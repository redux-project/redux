// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masterx.h"
#include "masterxman.h"
#include "stealthx.h"
#include "util.h"
#include "sync.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenMasterXScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if(nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if(mapCacheBlockHashes.count(nBlockHeight)){
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex *BlockLastSolved = chainActive.Tip();
    const CBlockIndex *BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight+1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if(nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight+1)-nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nBlocksAgo){
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CMasterX::CMasterX()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERX_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasterXPing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

CMasterX::CMasterX(const CMasterX& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubkey = other.pubkey;
    pubkey2 = other.pubkey2;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
}

CMasterX::CMasterX(const CMasterXBroadcast& gmb)
{
    LOCK(cs);
    vin = gmb.vin;
    addr = gmb.addr;
    pubkey = gmb.pubkey;
    pubkey2 = gmb.pubkey2;
    sig = gmb.sig;
    activeState = MASTERX_ENABLED;
    sigTime = gmb.sigTime;
    lastPing = gmb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = gmb.protocolVersion;
    nLastDsq = gmb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

//
// When a new masterx broadcast is sent, update our information
//
bool CMasterX::UpdateFromNewBroadcast(CMasterXBroadcast& gmb)
{
    if(gmb.sigTime > sigTime) {    
        pubkey2 = gmb.pubkey2;
        sigTime = gmb.sigTime;
        sig = gmb.sig;
        protocolVersion = gmb.protocolVersion;
        addr = gmb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if(gmb.lastPing == CMasterXPing() || (gmb.lastPing != CMasterXPing() && gmb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = gmb.lastPing;
            gmineman.mapSeenMasterXPing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a MasterX depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CMasterX::CalculateScore(int mod, int64_t nBlockHeight)
{
    if(chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if(!GetBlockHash(hash, nBlockHeight)) {
        LogPrintf("CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CMasterX::Check(bool forceCheck)
{
    if(ShutdownRequested()) return;

    if(!forceCheck && (GetTime() - lastTimeChecked < MASTERX_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if(activeState == MASTERX_VIN_SPENT) return;


    if(!IsPingedWithin(MASTERX_REMOVAL_SECONDS)){
        activeState = MASTERX_REMOVE;
        return;
    }

    if(!IsPingedWithin(MASTERX_EXPIRATION_SECONDS)){
        activeState = MASTERX_EXPIRED;
        return;
    }

    if(!unitTest){
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if(!lockMain) return;

            if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)){
                activeState = MASTERX_VIN_SPENT;
                return;

            }
        }
    }

    activeState = MASTERX_ENABLED; // OK
}

int64_t CMasterX::SecondsSincePayment() {
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60*60*24*30;
    if(sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CMasterX::GetLastPaid() {
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return false;

    CScript gmpayee;
    gmpayee = GetScriptForDestination(pubkey.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150; 

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex *BlockReading = chainActive.Tip();

    int nMnCount = gmineman.CountEnabled()*1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nMnCount){
            return 0;
        }
        n++;

        if(masterxPayments.mapMasterXBlocks.count(BlockReading->nHeight)){
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network 
                to converge on the same payees quickly, then keep the same schedule.
            */
            if(masterxPayments.mapMasterXBlocks[BlockReading->nHeight].HasPayeeWithVotes(gmpayee, 2)){
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

CMasterXBroadcast::CMasterXBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERX_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasterXPing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CMasterXBroadcast::CMasterXBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubkey = newPubkey;
    pubkey2 = newPubkey2;
    sig = std::vector<unsigned char>();
    activeState = MASTERX_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasterXPing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CMasterXBroadcast::CMasterXBroadcast(const CMasterX& gm)
{
    vin = gm.vin;
    addr = gm.addr;
    pubkey = gm.pubkey;
    pubkey2 = gm.pubkey2;
    sig = gm.sig;
    activeState = gm.activeState;
    sigTime = gm.sigTime;
    lastPing = gm.lastPing;
    cacheInputAge = gm.cacheInputAge;
    cacheInputAgeBlock = gm.cacheInputAgeBlock;
    unitTest = gm.unitTest;
    allowFreeTx = gm.allowFreeTx;
    protocolVersion = gm.protocolVersion;
    nLastDsq = gm.nLastDsq;
    nScanningErrorCount = gm.nScanningErrorCount;
    nLastScanningErrorBlockHeight = gm.nLastScanningErrorBlockHeight;
}

bool CMasterXBroadcast::CheckAndUpdate(int& nDos)
{
    nDos = 0;

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("gmb - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if(protocolVersion < masterxPayments.GetMinMasterXPaymentsProto()) {
        LogPrintf("gmb - ignoring outdated MasterX %s protocol version %d\n", vin.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("gmb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("gmb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if(!vin.scriptSig.empty()) {
        LogPrintf("gmb - Ignore Not Empty ScriptSig %s\n",vin.ToString());
        return false;
    }

    // incorrect ping or its sigTime
    if(lastPing == CMasterXPing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    std::string strMessage;
    std::string errorMessage = "";

    if(protocolVersion < 70201) {
        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("masterx", "gmb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
            SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
            EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            if (addr.ToString() != addr.ToString(false))
            {
                // maybe it's wrong format, try again with the old one
                strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                                vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

                LogPrint("masterx", "gmb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
                    SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
                    EncodeBase64(&sig[0], sig.size()));

                if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
                    // didn't work either
                    LogPrintf("gmb - Got bad MasterX address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                    // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                    return false;
                }
            } else {
                // nope, sig is actually wrong
                LogPrintf("gmb - Got bad MasterX address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                return false;
            }
        }
    } else {
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubkey.GetID().ToString() + pubkey2.GetID().ToString() +
                        boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("masterx", "gmb - strMessage: %s, pubkey address: %s, sig: %s\n",
            strMessage, CBitcoinAddress(pubkey.GetID()).ToString(), EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            LogPrintf("gmb - Got bad MasterX address signature, error: %s\n", errorMessage);
            nDos = 100;
            return false;
        }
    }

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != 9667) return false;
    } else if(addr.GetPort() == 9667) return false;

    //search existing MasterX list, this is where we update existing MasterXs with new gmb broadcasts
    CMasterX* pgm = gmineman.Find(vin);

    // no such masterx, nothing to update
    if(pgm == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenMasterXBroadcast in CMasterXMan::ProcessMessage should filter legit duplicates)
    if(pgm->sigTime >= sigTime) {
        LogPrintf("CMasterXBroadcast::CheckAndUpdate - Bad sigTime %d for MasterX %20s %105s (existing broadcast is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), pgm->sigTime);
        return false;
    }

    // masterx is not enabled yet/already, nothing to update
    if(!pgm->IsEnabled()) return true;

    // gm.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if(pgm->pubkey == pubkey && !pgm->IsBroadcastedWithin(MASTERX_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrintf("gmb - Got updated entry for %s\n", addr.ToString());
        if(pgm->UpdateFromNewBroadcast((*this))){
            pgm->Check();
            if(pgm->IsEnabled()) Relay();
        }
        masterxSync.AddedMasterXList(GetHash());
    }

    return true;
}

bool CMasterXBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a masterx with the same vin (i.e. already activated) and this gmb is ours (matches our MasterX privkey)
    // so nothing to do here for us
    if(fMasterX && vin.prevout == activeMasterX.vin.prevout && pubkey2 == activeMasterX.pubKeyMasterX)
        return true;

    // incorrect ping or its sigTime
    if(lastPing == CMasterXPing() || !lastPing.CheckAndUpdate(nDoS, false, true))
        return false;

    // search existing MasterX list
    CMasterX* pgm = gmineman.Find(vin);

    if(pgm != NULL) {
        // nothing to do here if we already know about this masterx and it's enabled
        if(pgm->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else gmineman.Remove(pgm->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) {
            // not gmb fault, let it to be checked again later
            gmineman.mapSeenMasterXBroadcast.erase(GetHash());
            masterxSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("masterx", "gmb - Accepted MasterX entry\n");

    if(GetInputAge(vin) < MASTERX_MIN_CONFIRMATIONS){
        LogPrintf("gmb - Input must have at least %d confirmations\n", MASTERX_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this gmb to be checked again later
        gmineman.mapSeenMasterXBroadcast.erase(GetHash());
        masterxSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 REDUX tx got MASTERX_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second)
    {
        CBlockIndex* pMNIndex = (*mi).second; // block for 1000 REDUX tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + MASTERX_MIN_CONFIRMATIONS - 1]; // block where tx got MASTERX_MIN_CONFIRMATIONS
        if(pConfIndex->GetBlockTime() > sigTime)
        {
            LogPrintf("gmb - Bad sigTime %d for MasterX %20s %105s (%i conf block is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), MASTERX_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrintf("gmb - Got NEW MasterX entry - %s - %s - %s - %lli \n", GetHash().ToString(), addr.ToString(), vin.ToString(), sigTime);
    CMasterX gm(*this);
    gmineman.Add(gm);

    // if it matches our MasterX privkey, then we've been remotely activated
    if(pubkey2 == activeMasterX.pubKeyMasterX && protocolVersion == PROTOCOL_VERSION){
        activeMasterX.EnableHotColdMasterX(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if(Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if(!isLocal) Relay();

    return true;
}

void CMasterXBroadcast::Relay()
{
    CInv inv(MSG_MASTERX_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CMasterXBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrintf("CMasterXBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CMasterXBroadcast::VerifySignature()
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)) {
        LogPrintf("CMasterXBroadcast::VerifySignature() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CMasterXPing::CMasterXPing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CMasterXPing::CMasterXPing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CMasterXPing::Sign(CKey& keyMasterX, CPubKey& pubKeyMasterX)
{
    std::string errorMessage;
    std::string strMasterXSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyMasterX)) {
        LogPrintf("CMasterXPing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyMasterX, vchSig, strMessage, errorMessage)) {
        LogPrintf("CMasterXPing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CMasterXPing::VerifySignature(CPubKey& pubKeyMasterX, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string errorMessage = "";

    if(!spySendSigner.VerifyMessage(pubKeyMasterX, vchSig, strMessage, errorMessage))
    {
        LogPrintf("CMasterXPing::VerifySignature - Got bad MasterX ping signature %s Error: %s\n", vin.ToString(), errorMessage);
        nDos = 33;
        return false;
    }
    return true;
}

bool CMasterXPing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CMasterXPing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrintf("CMasterXPing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if(fCheckSigTimeOnly) {
        CMasterX* pgm = gmineman.Find(vin);
        if(pgm) return VerifySignature(pgm->pubkey2, nDos);
        return true;
    }

    LogPrint("masterx", "CMasterXPing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this MasterX
    CMasterX* pgm = gmineman.Find(vin);
    if(pgm != NULL && pgm->protocolVersion >= masterxPayments.GetMinMasterXPaymentsProto())
    {
        if (fRequireEnabled && !pgm->IsEnabled()) return false;

        // LogPrintf("mnping - Found corresponding gm for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this masterx or
        // last ping was more then MASTERX_MIN_MNP_SECONDS-60 ago comparing to this one
        if(!pgm->IsPingedWithin(MASTERX_MIN_MNP_SECONDS - 60, sigTime))
        {
            if(!VerifySignature(pgm->pubkey2, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second)
            {
                if((*mi).second->nHeight < chainActive.Height() - 24)
                {
                    LogPrintf("CMasterXPing::CheckAndUpdate - MasterX %s block hash %s is too old\n", vin.ToString(), blockHash.ToString());
                    // Do nothing here (no MasterX update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrintf("CMasterXPing::CheckAndUpdate - MasterX %s block hash %s is unknown\n", vin.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pgm->lastPing = *this;

            //gmineman.mapSeenMasterXBroadcast.lastPing is probably outdated, so we'll update it
            CMasterXBroadcast gmb(*pgm);
            uint256 hash = gmb.GetHash();
            if(gmineman.mapSeenMasterXBroadcast.count(hash)) {
                gmineman.mapSeenMasterXBroadcast[hash].lastPing = *this;
            }

            pgm->Check(true);
            if(!pgm->IsEnabled()) return false;

            LogPrint("masterx", "CMasterXPing::CheckAndUpdate - MasterX ping accepted, vin: %s\n", vin.ToString());

            Relay();
            return true;
        }
        LogPrint("masterx", "CMasterXPing::CheckAndUpdate - MasterX ping arrived too early, vin: %s\n", vin.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("masterx", "CMasterXPing::CheckAndUpdate - Couldn't find compatible MasterX entry, vin: %s\n", vin.ToString());

    return false;
}

void CMasterXPing::Relay()
{
    CInv inv(MSG_MASTERX_PING, GetHash());
    RelayInv(inv);
}
