// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masterxman.h"
#include "masterx.h"
#include "activemasterx.h"
#include "stealthx.h"
#include "util.h"
#include "addrman.h"
#include "spork.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** MasterX manager */
CMasterXMan gmineman;

struct CompareLastPaid
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN
{
    bool operator()(const pair<int64_t, CMasterX>& t1,
                    const pair<int64_t, CMasterX>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CMasterXDB
//

CMasterXDB::CMasterXDB()
{
    pathMN = GetDataDir() / "gmcache.dat";
    strMagicMessage = "MasterXCache";
}

bool CMasterXDB::Write(const CMasterXMan& gminemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssMasterXs(SER_DISK, CLIENT_VERSION);
    ssMasterXs << strMagicMessage; // masterx cache file specific magic message
    ssMasterXs << FLATDATA(Params().MessageStart()); // network specific magic number
    ssMasterXs << gminemanToSave;
    uint256 hash = Hash(ssMasterXs.begin(), ssMasterXs.end());
    ssMasterXs << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssMasterXs;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
//    FileCommit(fileout);
    fileout.fclose();

    LogPrintf("Written info to gmcache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", gminemanToSave.ToString());

    return true;
}

CMasterXDB::ReadResult CMasterXDB::Read(CMasterXMan& gminemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssMasterXs(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssMasterXs.begin(), ssMasterXs.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masterx cache file specific magic message) and ..

        ssMasterXs >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid masterx cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssMasterXs >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CMasterXMan object
        ssMasterXs >> gminemanToLoad;
    }
    catch (std::exception &e) {
        gminemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from gmcache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", gminemanToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("MasterX manager - cleaning....\n");
        gminemanToLoad.CheckAndRemove(true);
        LogPrintf("MasterX manager - result:\n");
        LogPrintf("  %s\n", gminemanToLoad.ToString());
    }

    return Ok;
}

void DumpMasterXs()
{
    int64_t nStart = GetTimeMillis();

    CMasterXDB gmdb;
    CMasterXMan tempMnodeman;

    LogPrintf("Verifying gmcache.dat format...\n");
    CMasterXDB::ReadResult readResult = gmdb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasterXDB::FileError)
        LogPrintf("Missing masterx cache file - gmcache.dat, will try to recreate\n");
    else if (readResult != CMasterXDB::Ok)
    {
        LogPrintf("Error reading gmcache.dat: ");
        if(readResult == CMasterXDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to gmcache.dat...\n");
    gmdb.Write(gmineman);

    LogPrintf("MasterX dump finished  %dms\n", GetTimeMillis() - nStart);
}

CMasterXMan::CMasterXMan() {
    nDsqCount = 0;
}

bool CMasterXMan::Add(CMasterX &gm)
{
    LOCK(cs);

    if (!gm.IsEnabled())
        return false;

    CMasterX *pgm = Find(gm.vin);
    if (pgm == NULL)
    {
        LogPrint("masterx", "CMasterXMan: Adding new MasterX %s - %i now\n", gm.addr.ToString(), size() + 1);
        vMasterXs.push_back(gm);
        return true;
    }

    return false;
}

void CMasterXMan::AskForGM(CNode* pnode, CTxIn &vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasterXListEntry.find(vin.prevout);
    if (i != mWeAskedForMasterXListEntry.end())
    {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the gmb info once from the node that sent mnp

    LogPrintf("CMasterXMan::AskForGM - Asking node for missing entry, vin: %s\n", vin.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + MASTERX_MIN_MNP_SECONDS;
    mWeAskedForMasterXListEntry[vin.prevout] = askAgain;
}

void CMasterXMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH(CMasterX& gm, vMasterXs) {
        gm.Check();
    }
}

void CMasterXMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CMasterX>::iterator it = vMasterXs.begin();
    while(it != vMasterXs.end()){
        if((*it).activeState == CMasterX::MASTERX_REMOVE ||
                (*it).activeState == CMasterX::MASTERX_VIN_SPENT ||
                (forceExpiredRemoval && (*it).activeState == CMasterX::MASTERX_EXPIRED) ||
                (*it).protocolVersion < masterxPayments.GetMinMasterXPaymentsProto()) {
            LogPrint("masterx", "CMasterXMan: Removing inactive MasterX %s - %i now\n", (*it).addr.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them 
            //    sending a brand new gmb
            map<uint256, CMasterXBroadcast>::iterator it3 = mapSeenMasterXBroadcast.begin();
            while(it3 != mapSeenMasterXBroadcast.end()){
                if((*it3).second.vin == (*it).vin){
                    masterxSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMasterXBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masterx again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasterXListEntry.begin();
            while(it2 != mWeAskedForMasterXListEntry.end()){
                if((*it2).first == (*it).vin.prevout){
                    mWeAskedForMasterXListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vMasterXs.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the MasterX list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasterXList.begin();
    while(it1 != mAskedUsForMasterXList.end()){
        if((*it1).second < GetTime()) {
            mAskedUsForMasterXList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the MasterX list
    it1 = mWeAskedForMasterXList.begin();
    while(it1 != mWeAskedForMasterXList.end()){
        if((*it1).second < GetTime()){
            mWeAskedForMasterXList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which MasterXs we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasterXListEntry.begin();
    while(it2 != mWeAskedForMasterXListEntry.end()){
        if((*it2).second < GetTime()){
            mWeAskedForMasterXListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMasterXBroadcast
    map<uint256, CMasterXBroadcast>::iterator it3 = mapSeenMasterXBroadcast.begin();
    while(it3 != mapSeenMasterXBroadcast.end()){
        if((*it3).second.lastPing.sigTime < GetTime() - MASTERX_REMOVAL_SECONDS*2){
            LogPrint("masterx", "CMasterXMan::CheckAndRemove - Removing expired MasterX broadcast %s\n", (*it3).second.GetHash().ToString());
            masterxSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
            mapSeenMasterXBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMasterXPing
    map<uint256, CMasterXPing>::iterator it4 = mapSeenMasterXPing.begin();
    while(it4 != mapSeenMasterXPing.end()){
        if((*it4).second.sigTime < GetTime()-(MASTERX_REMOVAL_SECONDS*2)){
            mapSeenMasterXPing.erase(it4++);
        } else {
            ++it4;
        }
    }

}

void CMasterXMan::Clear()
{
    LOCK(cs);
    vMasterXs.clear();
    mAskedUsForMasterXList.clear();
    mWeAskedForMasterXList.clear();
    mWeAskedForMasterXListEntry.clear();
    mapSeenMasterXBroadcast.clear();
    mapSeenMasterXPing.clear();
    nDsqCount = 0;
}

int CMasterXMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? masterxPayments.GetMinMasterXPaymentsProto() : protocolVersion;

    BOOST_FOREACH(CMasterX& gm, vMasterXs) {
        gm.Check();
        if(gm.protocolVersion < protocolVersion || !gm.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMasterXMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())){
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasterXList.find(pnode->addr);
            if (it != mWeAskedForMasterXList.end())
            {
                if (GetTime() < (*it).second) {
                    LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                    return;
                }
            }
        }
    }
    
    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + MASTERXS_DSEG_SECONDS;
    mWeAskedForMasterXList[pnode->addr] = askAgain;
}

CMasterX *CMasterXMan::Find(const CScript &payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH(CMasterX& gm, vMasterXs)
    {
        payee2 = GetScriptForDestination(gm.pubkey.GetID());
        if(payee2 == payee)
            return &gm;
    }
    return NULL;
}

CMasterX *CMasterXMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CMasterX& gm, vMasterXs)
    {
        if(gm.vin.prevout == vin.prevout)
            return &gm;
    }
    return NULL;
}


CMasterX *CMasterXMan::Find(const CPubKey &pubKeyMasterX)
{
    LOCK(cs);

    BOOST_FOREACH(CMasterX& gm, vMasterXs)
    {
        if(gm.pubkey2 == pubKeyMasterX)
            return &gm;
    }
    return NULL;
}

// 
// Deterministically select the oldest/best masterx to pay on the network
//
CMasterX* CMasterXMan::GetNextMasterXInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CMasterX *pBestMasterX = NULL;
    std::vector<pair<int64_t, CTxIn> > vecMasterXLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH(CMasterX &gm, vMasterXs)
    {
        gm.Check();
        if(!gm.IsEnabled()) continue;

        // //check protocol version
        if(gm.protocolVersion < masterxPayments.GetMinMasterXPaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(masterxPayments.IsScheduled(gm, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && gm.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are masterxs
        if(gm.GetMasterXInputAge() < nMnCount) continue;

        vecMasterXLastPaid.push_back(make_pair(gm.SecondsSincePayment(), gm.vin));
    }

    nCount = (int)vecMasterXLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount/3) return GetNextMasterXInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecMasterXLastPaid.rbegin(), vecMasterXLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled()/10;
    int nCountTenth = 0; 
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecMasterXLastPaid){
        CMasterX* pgm = Find(s.second);
        if(!pgm) break;

        uint256 n = pgm->CalculateScore(1, nBlockHeight-100);
        if(n > nHigh){
            nHigh = n;
            pBestMasterX = pgm;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestMasterX;
}

CMasterX *CMasterXMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? masterxPayments.GetMinMasterXPaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrintf("CMasterXMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if(nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrintf("CMasterXMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH(CMasterX &gm, vMasterXs) {
        if(gm.protocolVersion < protocolVersion || !gm.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH(CTxIn &usedVin, vecToExclude) {
            if(gm.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if(found) continue;
        if(--rand < 1) {
            return &gm;
        }
    }

    return NULL;
}

CMasterX* CMasterXMan::GetCurrentMasterX(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CMasterX* winner = NULL;

    // scan for winner
    BOOST_FOREACH(CMasterX& gm, vMasterXs) {
        gm.Check();
        if(gm.protocolVersion < minProtocol || !gm.IsEnabled()) continue;

        // calculate the score for each MasterX
        uint256 n = gm.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if(n2 > score){
            score = n2;
            winner = &gm;
        }
    }

    return winner;
}

int CMasterXMan::GetMasterXRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMasterXScores;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH(CMasterX& gm, vMasterXs) {
        if(gm.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            gm.Check();
            if(!gm.IsEnabled()) continue;
        }
        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasterXScores.push_back(make_pair(n2, gm.vin));
    }

    sort(vecMasterXScores.rbegin(), vecMasterXScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecMasterXScores){
        rank++;
        if(s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CMasterX> > CMasterXMan::GetMasterXRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CMasterX> > vecMasterXScores;
    std::vector<pair<int, CMasterX> > vecMasterXRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return vecMasterXRanks;

    // scan for winner
    BOOST_FOREACH(CMasterX& gm, vMasterXs) {

        gm.Check();

        if(gm.protocolVersion < minProtocol) continue;
        if(!gm.IsEnabled()) {
            continue;
        }

        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasterXScores.push_back(make_pair(n2, gm));
    }

    sort(vecMasterXScores.rbegin(), vecMasterXScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMasterX)& s, vecMasterXScores){
        rank++;
        vecMasterXRanks.push_back(make_pair(rank, s.second));
    }

    return vecMasterXRanks;
}

CMasterX* CMasterXMan::GetMasterXByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMasterXScores;

    // scan for winner
    BOOST_FOREACH(CMasterX& gm, vMasterXs) {

        if(gm.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            gm.Check();
            if(!gm.IsEnabled()) continue;
        }

        uint256 n = gm.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasterXScores.push_back(make_pair(n2, gm.vin));
    }

    sort(vecMasterXScores.rbegin(), vecMasterXScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecMasterXScores){
        rank++;
        if(rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CMasterXMan::ProcessMasterXConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fStealthXMaster){
            if(spySendPool.pSubmittedToMasterX != NULL && pnode->addr == spySendPool.pSubmittedToMasterX->addr) continue;
            LogPrintf("Closing MasterX connection %s \n", pnode->addr.ToString());
            pnode->fStealthXMaster = false;
            pnode->Release();
        }
    }
}

void CMasterXMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if(fLiteMode) return; //disable all StealthX/MasterX related functionality
    if(!masterxSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "gmb") { //MasterX Broadcast
        CMasterXBroadcast gmb;
        vRecv >> gmb;

        int nDoS = 0;
        if (CheckMnbAndUpdateMasterXList(gmb, nDoS)) {
            // use announced MasterX as a peer
             addrman.Add(CAddress(gmb.addr), pfrom->addr, 2*60*60);
        } else {
            if(nDoS > 0) Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //MasterX Ping
        CMasterXPing mnp;
        vRecv >> mnp;

        LogPrint("masterx", "mnp - MasterX ping, vin: %s\n", mnp.vin.ToString());

        if(mapSeenMasterXPing.count(mnp.GetHash())) return; //seen
        mapSeenMasterXPing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if(mnp.CheckAndUpdate(nDoS)) return;

        if(nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing MasterX list
            CMasterX* pgm = Find(mnp.vin);
            // if it's known, don't ask for the gmb, just return
            if(pgm != NULL) return;
        }

        // something significant is broken or gm is unknown,
        // we might have to ask for a masterx entry once
        AskForGM(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get MasterX list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasterXList.find(pfrom->addr);
                if (i != mAskedUsForMasterXList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERXS_DSEG_SECONDS;
                mAskedUsForMasterXList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH(CMasterX& gm, vMasterXs) {
            if(gm.addr.IsRFC1918()) continue; //local network

            if(gm.IsEnabled()) {
                LogPrint("masterx", "dseg - Sending MasterX entry - %s \n", gm.addr.ToString());
                if(vin == CTxIn() || vin == gm.vin){
                    CMasterXBroadcast gmb = CMasterXBroadcast(gm);
                    uint256 hash = gmb.GetHash();
                    pfrom->PushInventory(CInv(MSG_MASTERX_ANNOUNCE, hash));
                    nInvCount++;

                    if(!mapSeenMasterXBroadcast.count(hash)) mapSeenMasterXBroadcast.insert(make_pair(hash, gmb));

                    if(vin == gm.vin) {
                        LogPrintf("dseg - Sent 1 MasterX entries to %s\n", pfrom->addr.ToString());
                        return;
                    }
                }
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage("ssc", MASTERX_SYNC_LIST, nInvCount);
            LogPrintf("dseg - Sent %d MasterX entries to %s\n", nInvCount, pfrom->addr.ToString());
        }
    }

}

void CMasterXMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CMasterX>::iterator it = vMasterXs.begin();
    while(it != vMasterXs.end()){
        if((*it).vin == vin){
            LogPrint("masterx", "CMasterXMan: Removing MasterX %s - %i now\n", (*it).addr.ToString(), size() - 1);
            vMasterXs.erase(it);
            break;
        }
        ++it;
    }
}

std::string CMasterXMan::ToString() const
{
    std::ostringstream info;

    info << "MasterXs: " << (int)vMasterXs.size() <<
            ", peers who asked us for MasterX list: " << (int)mAskedUsForMasterXList.size() <<
            ", peers we asked for MasterX list: " << (int)mWeAskedForMasterXList.size() <<
            ", entries in MasterX list we asked for: " << (int)mWeAskedForMasterXListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CMasterXMan::UpdateMasterXList(CMasterXBroadcast gmb) {
    mapSeenMasterXPing.insert(make_pair(gmb.lastPing.GetHash(), gmb.lastPing));
    mapSeenMasterXBroadcast.insert(make_pair(gmb.GetHash(), gmb));
    masterxSync.AddedMasterXList(gmb.GetHash());

    LogPrintf("CMasterXMan::UpdateMasterXList() - addr: %s\n    vin: %s\n", gmb.addr.ToString(), gmb.vin.ToString());

    CMasterX* pgm = Find(gmb.vin);
    if(pgm == NULL)
    {
        CMasterX gm(gmb);
        Add(gm);
    } else {
        pgm->UpdateFromNewBroadcast(gmb);
    }
}

bool CMasterXMan::CheckMnbAndUpdateMasterXList(CMasterXBroadcast gmb, int& nDos) {
    nDos = 0;
    LogPrint("masterx", "CMasterXMan::CheckMnbAndUpdateMasterXList - MasterX broadcast, vin: %s\n", gmb.vin.ToString());

    if(mapSeenMasterXBroadcast.count(gmb.GetHash())) { //seen
        masterxSync.AddedMasterXList(gmb.GetHash());
        return true;
    }
    mapSeenMasterXBroadcast.insert(make_pair(gmb.GetHash(), gmb));

    LogPrint("masterx", "CMasterXMan::CheckMnbAndUpdateMasterXList - MasterX broadcast, vin: %s new\n", gmb.vin.ToString());

    if(!gmb.CheckAndUpdate(nDos)){
        LogPrint("masterx", "CMasterXMan::CheckMnbAndUpdateMasterXList - MasterX broadcast, vin: %s CheckAndUpdate failed\n", gmb.vin.ToString());
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the MasterX
    //  - this is expensive, so it's only done once per MasterX
    if(!spySendSigner.IsVinAssociatedWithPubkey(gmb.vin, gmb.pubkey)) {
        LogPrintf("CMasterXMan::CheckMnbAndUpdateMasterXList - Got mismatched pubkey and vin\n");
        nDos = 33;
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckStealthXPool()
    if(gmb.CheckInputsAndAdd(nDos)) {
        masterxSync.AddedMasterXList(gmb.GetHash());
    } else {
        LogPrintf("CMasterXMan::CheckMnbAndUpdateMasterXList - Rejected MasterX entry %s\n", gmb.addr.ToString());
        return false;
    }

    return true;
}