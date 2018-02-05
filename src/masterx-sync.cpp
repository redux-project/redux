// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "activemasterx.h"
#include "masterx-sync.h"
#include "masterx-payments.h"
#include "masterx-evolution.h"
#include "masterx.h"
#include "masterxman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"

class CMasterXSync;
CMasterXSync masterxSync;

CMasterXSync::CMasterXSync()
{
    Reset();
}

bool CMasterXSync::IsSynced()
{
    return RequestedMasterXAssets == MASTERX_SYNC_FINISHED;
}

bool CMasterXSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - lastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if(fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if(!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if(pindex == NULL) return false;


    if(pindex->nTime + 60*60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CMasterXSync::Reset()
{   
    lastMasterXList = 0;
    lastMasterXWinner = 0;
    lastEvolutionItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncGMW.clear();
    mapSeenSyncEvolution.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumMasterXList = 0;
    sumMasterXWinner = 0;
    sumEvolutionItemProp = 0;
    sumEvolutionItemFin = 0;
    countMasterXList = 0;
    countMasterXWinner = 0;
    countEvolutionItemProp = 0;
    countEvolutionItemFin = 0;
    RequestedMasterXAssets = MASTERX_SYNC_INITIAL;
    RequestedMasterXAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CMasterXSync::AddedMasterXList(uint256 hash)
{
    if(gmineman.mapSeenMasterXBroadcast.count(hash)) {
        if(mapSeenSyncMNB[hash] < MASTERX_SYNC_THRESHOLD) {
            lastMasterXList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastMasterXList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CMasterXSync::AddedMasterXWinner(uint256 hash)
{
    if(masterxPayments.mapMasterXPayeeVotes.count(hash)) {
        if(mapSeenSyncGMW[hash] < MASTERX_SYNC_THRESHOLD) {
            lastMasterXWinner = GetTime();
            mapSeenSyncGMW[hash]++;
        }
    } else {
        lastMasterXWinner = GetTime();
        mapSeenSyncGMW.insert(make_pair(hash, 1));
    }
}

void CMasterXSync::AddedEvolutionItem(uint256 hash)
{
    if(evolution.mapSeenMasterXEvolutionProposals.count(hash) || evolution.mapSeenMasterXEvolutionVotes.count(hash) ||
            evolution.mapSeenFinalizedEvolutions.count(hash) || evolution.mapSeenFinalizedEvolutionVotes.count(hash)) {
        if(mapSeenSyncEvolution[hash] < MASTERX_SYNC_THRESHOLD) {
            lastEvolutionItem = GetTime();
            mapSeenSyncEvolution[hash]++;
        }
    } else {
        lastEvolutionItem = GetTime();
        mapSeenSyncEvolution.insert(make_pair(hash, 1));
    }
}

bool CMasterXSync::IsEvolutionPropEmpty()
{
    return sumEvolutionItemProp==0 && countEvolutionItemProp>0;
}

bool CMasterXSync::IsEvolutionFinEmpty()
{
    return sumEvolutionItemFin==0 && countEvolutionItemFin>0;
}

void CMasterXSync::GetNextAsset()
{
    switch(RequestedMasterXAssets)
    {
        case(MASTERX_SYNC_INITIAL):
        case(MASTERX_SYNC_FAILED): // should never be used here actually, use Reset() instead
            ClearFulfilledRequest();
            RequestedMasterXAssets = MASTERX_SYNC_SPORKS;
            break;
        case(MASTERX_SYNC_SPORKS):
            RequestedMasterXAssets = MASTERX_SYNC_LIST;
            break;
        case(MASTERX_SYNC_LIST):
            RequestedMasterXAssets = MASTERX_SYNC_GMW;
            break;
        case(MASTERX_SYNC_GMW):
            RequestedMasterXAssets = MASTERX_SYNC_EVOLUTION;
            break;
        case(MASTERX_SYNC_EVOLUTION):
            LogPrintf("CMasterXSync::GetNextAsset - Sync has finished\n");
            RequestedMasterXAssets = MASTERX_SYNC_FINISHED;
            break;
    }
    RequestedMasterXAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMasterXSync::GetSyncStatus()
{
    switch (masterxSync.RequestedMasterXAssets) {
        case MASTERX_SYNC_INITIAL: return _("Synchronization pending...");
        case MASTERX_SYNC_SPORKS: return _("Synchronizing sporks...");
        case MASTERX_SYNC_LIST: return _("Synchronizing masterxs...");
        case MASTERX_SYNC_GMW: return _("Synchronizing masterx winners...");
        case MASTERX_SYNC_EVOLUTION: return _("Synchronizing evolutions...");
        case MASTERX_SYNC_FAILED: return _("Synchronization failed");
        case MASTERX_SYNC_FINISHED: return _("Synchronization finished");
    }
    return "";
}

void CMasterXSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if(RequestedMasterXAssets >= MASTERX_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch(nItemID)
        {
            case(MASTERX_SYNC_LIST):
                if(nItemID != RequestedMasterXAssets) return;
                sumMasterXList += nCount;
                countMasterXList++;
                break;
            case(MASTERX_SYNC_GMW):
                if(nItemID != RequestedMasterXAssets) return;
                sumMasterXWinner += nCount;
                countMasterXWinner++;
                break;
            case(MASTERX_SYNC_EVOLUTION_PROP):
                if(RequestedMasterXAssets != MASTERX_SYNC_EVOLUTION) return;
                sumEvolutionItemProp += nCount;
                countEvolutionItemProp++;
                break;
            case(MASTERX_SYNC_EVOLUTION_FIN):
                if(RequestedMasterXAssets != MASTERX_SYNC_EVOLUTION) return;
                sumEvolutionItemFin += nCount;
                countEvolutionItemFin++;
                break;
        }
        
        LogPrintf("CMasterXSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CMasterXSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("gmsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CMasterXSync::Process()
{
    static int tick = 0;

    if(tick++ % MASTERX_SYNC_TIMEOUT != 0) return;

    if(IsSynced()) {
        /* 
            Resync if we lose all masterxs from sleep/wake or failure to sync originally
        */
        if(gmineman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if(RequestedMasterXAssets == MASTERX_SYNC_FAILED && lastFailure + (1*60) < GetTime()) {
        Reset();
    } else if (RequestedMasterXAssets == MASTERX_SYNC_FAILED) {
        return;
    }

    if(fDebug) LogPrintf("CMasterXSync::Process() - tick %d RequestedMasterXAssets %d\n", tick, RequestedMasterXAssets);

    if(RequestedMasterXAssets == MASTERX_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkID() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && RequestedMasterXAssets > MASTERX_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(Params().NetworkID() == CBaseChainParams::REGTEST){
            if(RequestedMasterXAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if(RequestedMasterXAttempt < 4) {
                gmineman.DsegUpdate(pnode); 
            } else if(RequestedMasterXAttempt < 6) {
                int nMnCount = gmineman.CountEnabled();
                pnode->PushMessage("gmget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("gmvs", n); //sync masterx votes
            } else {
                RequestedMasterXAssets = MASTERX_SYNC_FINISHED;
            }
            RequestedMasterXAttempt++;
            return;
        }

        //set to synced
        if(RequestedMasterXAssets == MASTERX_SYNC_SPORKS){
            if(pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if(RequestedMasterXAttempt >= 2) GetNextAsset();
            RequestedMasterXAttempt++;
            
            return;
        }

        if (pnode->nVersion >= masterxPayments.GetMinMasterXPaymentsProto()) {

            if(RequestedMasterXAssets == MASTERX_SYNC_LIST) {
                if(fDebug) LogPrintf("CMasterXSync::Process() - lastMasterXList %lld (GetTime() - MASTERX_SYNC_TIMEOUT) %lld\n", lastMasterXList, GetTime() - MASTERX_SYNC_TIMEOUT);
                if(lastMasterXList > 0 && lastMasterXList < GetTime() - MASTERX_SYNC_TIMEOUT*2 && RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("gmsync")) continue;
                pnode->FulfilledRequest("gmsync");

                // timeout
                if(lastMasterXList == 0 &&
                (RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > MASTERX_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_MASTERX_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMasterXSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMasterXAssets = MASTERX_SYNC_FAILED;
                        RequestedMasterXAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3) return;

                gmineman.DsegUpdate(pnode);
                RequestedMasterXAttempt++;
                return;
            }

            if(RequestedMasterXAssets == MASTERX_SYNC_GMW) {
                if(lastMasterXWinner > 0 && lastMasterXWinner < GetTime() - MASTERX_SYNC_TIMEOUT*2 && RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if(lastMasterXWinner == 0 &&
                (RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > MASTERX_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_MASTERX_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMasterXSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMasterXAssets = MASTERX_SYNC_FAILED;
                        RequestedMasterXAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if(pindexPrev == NULL) return;

                int nMnCount = gmineman.CountEnabled();
                pnode->PushMessage("gmget", nMnCount); //sync payees
                RequestedMasterXAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= MIN_EVOLUTION_PEER_PROTO_VERSION) {

            if(RequestedMasterXAssets == MASTERX_SYNC_EVOLUTION){
                //we'll start rejecting votes if we accidentally get set as synced too soon
                if(lastEvolutionItem > 0 && lastEvolutionItem < GetTime() - MASTERX_SYNC_TIMEOUT*2 && RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    //LogPrintf("CMasterXSync::Process - HasNextFinalizedEvolution %d nCountFailures %d IsEvolutionPropEmpty %d\n", evolution.HasNextFinalizedEvolution(), nCountFailures, IsEvolutionPropEmpty());
                    //if(evolution.HasNextFinalizedEvolution() || nCountFailures >= 2 || IsEvolutionPropEmpty()) {
                        GetNextAsset();

                        //try to activate our masterx if possible
                        activeMasterX.ManageStatus();
                    // } else { //we've failed to sync, this state will reject the next evolution block
                    //     LogPrintf("CMasterXSync::Process - ERROR - Sync has failed, will retry later\n");
                    //     RequestedMasterXAssets = MASTERX_SYNC_FAILED;
                    //     RequestedMasterXAttempt = 0;
                    //     lastFailure = GetTime();
                    //     nCountFailures++;
                    // }
                    return;
                }

                // timeout
                if(lastEvolutionItem == 0 &&
                (RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > MASTERX_SYNC_TIMEOUT*5)) {
                    // maybe there is no evolutions at all, so just finish syncing
                    GetNextAsset();
                    activeMasterX.ManageStatus();
                    return;
                }

                if(pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if(RequestedMasterXAttempt >= MASTERX_SYNC_THRESHOLD*3) return;

                uint256 n = 0;
                pnode->PushMessage("gmvs", n); //sync masterx votes
                RequestedMasterXAttempt++;
                
                return;
            }

        }
    }
}
