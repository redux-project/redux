// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masterx-payments.h"
#include "masterx-evolution.h"
#include "masterx-sync.h"
#include "masterxman.h"
#include "stealthx.h"
#include "util.h"
#include "sync.h"
#include "spork.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Object for who's going to get paid on which blocks */
CMasterXPayments masterxPayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMasterXBlocks;
CCriticalSection cs_mapMasterXPayeeVotes;

//
// CMasterXPaymentDB
//

CMasterXPaymentDB::CMasterXPaymentDB()
{
    pathDB = GetDataDir() / "gmpayments.dat";
    strMagicMessage = "MasterXPayments";
}

bool CMasterXPaymentDB::Write(const CMasterXPayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage; // masterx cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrintf("Written info to gmpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CMasterXPaymentDB::ReadResult CMasterXPaymentDB::Read(CMasterXPayments& objToLoad, bool fDryRun)
{

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masterx cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid masterx payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMasterXPayments object
        ssObj >> objToLoad;
    }
    catch (std::exception &e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from gmpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", objToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("MasterX payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrintf("MasterX payments manager - result:\n");
        LogPrintf("  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpMasterXPayments()
{
    int64_t nStart = GetTimeMillis();

    CMasterXPaymentDB paymentdb;
    CMasterXPayments tempPayments;

    LogPrintf("Verifying gmpayments.dat format...\n");
    CMasterXPaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasterXPaymentDB::FileError)
        LogPrintf("Missing evolutions file - gmpayments.dat, will try to recreate\n");
    else if (readResult != CMasterXPaymentDB::Ok)
    {
        LogPrintf("Error reading gmpayments.dat: ");
        if(readResult == CMasterXPaymentDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to gmpayments.dat...\n");
    paymentdb.Write(masterxPayments);

    LogPrintf("Evolution dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue){
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return true;

    int nHeight = 0;
    if(pindexPrev->GetBlockHash() == block.hashPrevBlock)
    {
        nHeight = pindexPrev->nHeight+1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight+1;
    }

    if(nHeight == 0){
        LogPrintf("IsBlockValueValid() : WARNING: Couldn't find previous block");
    }

    if(!masterxSync.IsSynced()) { //there is no evolution data to use to check anything
        //super blocks will always be on these blocks, max 100 per evolutioning
        if(nHeight % GetEvolutionPaymentCycleBlocks() < 100){
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    } else { // we're synced and have data so check the evolution schedule

        //are these blocks even enabled
        if(!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
            return block.vtx[0].GetValueOut() <= nExpectedValue;
        }
        
        if(evolution.IsEvolutionPaymentBlock(nHeight)){
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight)
{
    if(!masterxSync.IsSynced()) { //there is no evolution data to use to check anything -- find the longest chain
        LogPrint("gmpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    //check if it's a evolution block
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
        if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
            if(evolution.IsTransactionValid(txNew, nBlockHeight)){
                return true;
            } else {
                LogPrintf("Invalid evolution payment detected %s\n", txNew.ToString().c_str());
                if(IsSporkActive(SPORK_9_MASTERX_EVOLUTION_ENFORCEMENT)){
                    return false;
                } else {
                    LogPrintf("Evolution enforcement is disabled, accepting block\n");
                    return true;
                }
            }
        }
    }

    //check for masterx payee
    if(masterxPayments.IsTransactionValid(txNew, nBlockHeight))
    {
        return true;
    } else {
        LogPrintf("Invalid gm payment detected %s\n", txNew.ToString().c_str());
        if(IsSporkActive(SPORK_8_MASTERX_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("MasterX payment enforcement is disabled, accepting block\n");
            return true;
        }
    }

    return false;
}


void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(pindexPrev->nHeight+1)){
        evolution.FillBlockPayee(txNew, nFees);
    } else {
        masterxPayments.FillBlockPayee(txNew, nFees);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        return evolution.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return masterxPayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CMasterXPayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if(!masterxPayments.GetBlockPayee(pindexPrev->nHeight+1, payee)){
        //no masterx detected
        CMasterX* winningNode = gmineman.GetCurrentMasterX(1);
        if(winningNode){
            payee = GetScriptForDestination(winningNode->pubkey.GetID());
        } else {
            LogPrintf("CreateNewBlock: Failed to detect masterx to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nBits, pindexPrev->nHeight, nFees);
    CAmount masterxPayment = GetMasterXPayment(pindexPrev->nHeight+1, blockValue);

    txNew.vout[0].nValue = blockValue;

    if(hasPayment && pindexPrev->nHeight+1>1){
        txNew.vout.resize(2);

        txNew.vout[1].scriptPubKey = payee;
        txNew.vout[1].nValue = masterxPayment;

        txNew.vout[0].nValue -= masterxPayment;

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("MasterX payment to %s\n", address2.ToString().c_str());
    }
}

int CMasterXPayments::GetMinMasterXPaymentsProto() {
    return IsSporkActive(SPORK_10_MASTERX_PAY_UPDATED_NODES)
            ? MIN_MASTERX_PAYMENT_PROTO_VERSION_2
            : MIN_MASTERX_PAYMENT_PROTO_VERSION_1;
}

void CMasterXPayments::ProcessMessageMasterXPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(!masterxSync.IsBlockchainSynced()) return;

    if(fLiteMode) return; //disable all StealthX/MasterX related functionality


    if (strCommand == "gmget") { //MasterX Payments Request Sync
        if(fLiteMode) return; //disable all StealthX/MasterX related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(Params().NetworkID() == CBaseChainParams::MAIN){
            if(pfrom->HasFulfilledRequest("gmget")) {
                LogPrintf("gmget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("gmget");
        masterxPayments.Sync(pfrom, nCountNeeded);
        LogPrintf("gmget - Sent MasterX winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "mnw") { //MasterX Payments Declare Winner
        //this is required in litemodef
        CMasterXPaymentWinner winner;
        vRecv >> winner;

        if(pfrom->nVersion < MIN_GMW_PEER_PROTO_VERSION) return;

        int nHeight;
		{
			TRY_LOCK(cs_main, locked);
			if(!locked || chainActive.Tip() == NULL) return;
			nHeight = chainActive.Tip()->nHeight;
		}

        if(masterxPayments.mapMasterXPayeeVotes.count(winner.GetHash())){
            LogPrint("gmpayments", "gmw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masterxSync.AddedMasterXWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (gmineman.CountEnabled()*1.25);
         if(winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight+20){
            LogPrint("gmpayments", "gmw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if(!winner.IsValid(pfrom, strError)){
            if(strError != "") LogPrintf("gmw - invalid message - %s\n", strError);
            return;
        }

        if(!masterxPayments.CanVote(winner.vinMasterX.prevout, winner.nBlockHeight)){
            LogPrintf("gmw - masterx already voted - %s\n", winner.vinMasterX.prevout.ToStringShort());
            return;
        }

        if(!winner.SignatureValid()){
            LogPrintf("gmw - invalid signature\n");
            if(masterxSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced masterx
            gmineman.AskForGM(pfrom, winner.vinMasterX);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("gmpayments", "gmw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinMasterX.prevout.ToStringShort());

        if(masterxPayments.AddWinningMasterX(winner)){
            winner.Relay();
            masterxSync.AddedMasterXWinner(winner.GetHash());
        }
    }
}

bool CMasterXPaymentWinner::Sign(CKey& keyMasterX, CPubKey& pubKeyMasterX)
{
    std::string errorMessage;
    std::string strMasterXSignMessage;

    std::string strMessage =  vinMasterX.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payee.ToString();

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyMasterX)) {
        LogPrintf("CMasterXPing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyMasterX, vchSig, strMessage, errorMessage)) {
        LogPrintf("CMasterXPing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CMasterXPayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapMasterXBlocks.count(nBlockHeight)){
        return mapMasterXBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masterx scheduled to get paid soon? 
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasterXPayments::IsScheduled(CMasterX& gm, int nNotBlockHeight)
{
    LOCK(cs_mapMasterXBlocks);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return false;
		nHeight = chainActive.Tip()->nHeight;
	}
    
    CScript gmpayee;
    gmpayee = GetScriptForDestination(gm.pubkey.GetID());

    CScript payee;
    for(int64_t h = nHeight; h <= nHeight+8; h++){
        if(h == nNotBlockHeight) continue;
        if(mapMasterXBlocks.count(h)){
            if(mapMasterXBlocks[h].GetPayee(payee)){
                if(gmpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasterXPayments::AddWinningMasterX(CMasterXPaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasterXPayeeVotes, cs_mapMasterXBlocks);
    
        if(mapMasterXPayeeVotes.count(winnerIn.GetHash())){
           return false;
        }

        mapMasterXPayeeVotes[winnerIn.GetHash()] = winnerIn;

        if(!mapMasterXBlocks.count(winnerIn.nBlockHeight)){
           CMasterXBlockPayees blockPayees(winnerIn.nBlockHeight);
           mapMasterXBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    int n = 1;
    if(IsReferenceNode(winnerIn.vinMasterX)) n = 100;
    mapMasterXBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, n);

    return true;
}

bool CMasterXBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount masterxPayment = GetMasterXPayment(nBlockHeight, txNew.GetValueOut());

    //require at least 6 signatures

    BOOST_FOREACH(CMasterXPayee& payee, vecPayments)
        if(payee.nVotes >= nMaxSignatures && payee.nVotes >= GMPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < GMPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CMasterXPayee& payee, vecPayments)
    {
        bool found = false;
        BOOST_FOREACH(CTxOut out, txNew.vout){
            if(payee.scriptPubKey == out.scriptPubKey && masterxPayment == out.nValue){
                found = true;
            }
        }

        if(payee.nVotes >= GMPAYMENTS_SIGNATURES_REQUIRED){
            if(found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == ""){
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }


    LogPrintf("CMasterXPayments::IsTransactionValid - Missing required payment - %s\n", strPayeesPossible.c_str());
    return false;
}

std::string CMasterXBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH(CMasterXPayee& payee, vecPayments)
    {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if(ret != "Unknown"){
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CMasterXPayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasterXBlocks);

    if(mapMasterXBlocks.count(nBlockHeight)){
        return mapMasterXBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasterXPayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasterXBlocks);

    if(mapMasterXBlocks.count(nBlockHeight)){
        return mapMasterXBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMasterXPayments::CleanPaymentList()
{
    LOCK2(cs_mapMasterXPayeeVotes, cs_mapMasterXBlocks);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return;
		nHeight = chainActive.Tip()->nHeight;
	}

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(gmineman.size()*1.25), 1000);

    std::map<uint256, CMasterXPaymentWinner>::iterator it = mapMasterXPayeeVotes.begin();
    while(it != mapMasterXPayeeVotes.end()) {
        CMasterXPaymentWinner winner = (*it).second;

        if(nHeight - winner.nBlockHeight > nLimit){
            LogPrint("gmpayments", "CMasterXPayments::CleanPaymentList - Removing old MasterX payment - block %d\n", winner.nBlockHeight);
            masterxSync.mapSeenSyncGMW.erase((*it).first);
            mapMasterXPayeeVotes.erase(it++);
            mapMasterXBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool IsReferenceNode(CTxIn& vin)
{
    //reference node - hybrid mode
    if(vin.prevout.ToStringShort() == "099c01bea63abd1692f60806bb646fa1d288e2d049281225f17e499024084e28-0") return true; // mainnet
    if(vin.prevout.ToStringShort() == "fbc16ae5229d6d99181802fd76a4feee5e7640164dcebc7f8feb04a7bea026f8-0") return true; // testnet
    if(vin.prevout.ToStringShort() == "e466f5d8beb4c2d22a314310dc58e0ea89505c95409754d0d68fb874952608cc-1") return true; // regtest

    return false;
}

bool CMasterXPaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    if(IsReferenceNode(vinMasterX)) return true;

    CMasterX* pgm = gmineman.Find(vinMasterX);

    if(!pgm)
    {
        strError = strprintf("Unknown MasterX %s", vinMasterX.prevout.ToStringShort());
        LogPrintf ("CMasterXPaymentWinner::IsValid - %s\n", strError);
        gmineman.AskForGM(pnode, vinMasterX);
        return false;
    }

    if(pgm->protocolVersion < MIN_GMW_PEER_PROTO_VERSION)
    {
        strError = strprintf("MasterX protocol too old %d - req %d", pgm->protocolVersion, MIN_GMW_PEER_PROTO_VERSION);
        LogPrintf ("CMasterXPaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = gmineman.GetMasterXRank(vinMasterX, nBlockHeight-100, MIN_GMW_PEER_PROTO_VERSION);

    if(n > GMPAYMENTS_SIGNATURES_TOTAL)
    {    
        //It's common to have masterxs mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if(n > GMPAYMENTS_SIGNATURES_TOTAL*2)
        {
            strError = strprintf("MasterX not in the top %d (%d)", GMPAYMENTS_SIGNATURES_TOTAL, n);
            LogPrintf("CMasterXPaymentWinner::IsValid - %s\n", strError);
            if(masterxSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CMasterXPayments::ProcessBlock(int nBlockHeight)
{
    if(!fMasterX) return false;

    //reference node - hybrid mode

    if(!IsReferenceNode(activeMasterX.vin)){
        int n = gmineman.GetMasterXRank(activeMasterX.vin, nBlockHeight-100, MIN_GMW_PEER_PROTO_VERSION);

        if(n == -1)
        {
            LogPrint("gmpayments", "CMasterXPayments::ProcessBlock - Unknown MasterX\n");
            return false;
        }

        if(n > GMPAYMENTS_SIGNATURES_TOTAL)
        {
            LogPrint("gmpayments", "CMasterXPayments::ProcessBlock - MasterX not in the top %d (%d)\n", GMPAYMENTS_SIGNATURES_TOTAL, n);
            return false;
        }
    }

    if(nBlockHeight <= nLastBlockHeight) return false;

    CMasterXPaymentWinner newWinner(activeMasterX.vin);

    if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        //is evolution payment block -- handled by the evolutioning software
    } else {
        LogPrintf("CMasterXPayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasterX.vin.ToString().c_str());

        // pay to the oldest GM that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMasterX *pgm = gmineman.GetNextMasterXInQueueForPayment(nBlockHeight, true, nCount);
        
        if(pgm != NULL)
        {
            LogPrintf("CMasterXPayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pgm->pubkey.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrintf("CMasterXPayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrintf("CMasterXPayments::ProcessBlock() Failed to find masterx to pay\n");
        }

    }

    std::string errorMessage;
    CPubKey pubKeyMasterX;
    CKey keyMasterX;

    if(!spySendSigner.SetKey(strMasterXPrivKey, errorMessage, keyMasterX, pubKeyMasterX))
    {
        LogPrintf("CMasterXPayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrintf("CMasterXPayments::ProcessBlock() - Signing Winner\n");
    if(newWinner.Sign(keyMasterX, pubKeyMasterX))
    {
        LogPrintf("CMasterXPayments::ProcessBlock() - AddWinningMasterX\n");

        if(AddWinningMasterX(newWinner))
        {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CMasterXPaymentWinner::Relay()
{
    CInv inv(MSG_MASTERX_WINNER, GetHash());
    RelayInv(inv);
}

bool CMasterXPaymentWinner::SignatureValid()
{

    CMasterX* pgm = gmineman.Find(vinMasterX);

    if(pgm != NULL)
    {
        std::string strMessage =  vinMasterX.prevout.ToStringShort() +
                    boost::lexical_cast<std::string>(nBlockHeight) +
                    payee.ToString();

        std::string errorMessage = "";
        if(!spySendSigner.VerifyMessage(pgm->pubkey2, vchSig, strMessage, errorMessage)){
            return error("CMasterXPaymentWinner::SignatureValid() - Got bad MasterX address signature %s \n", vinMasterX.ToString().c_str());
        }

        return true;
    }

    return false;
}

void CMasterXPayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapMasterXPayeeVotes);

    int nHeight;
	{
		TRY_LOCK(cs_main, locked);
		if(!locked || chainActive.Tip() == NULL) return;
		nHeight = chainActive.Tip()->nHeight;
	}

    int nCount = (gmineman.CountEnabled()*1.25);
    if(nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasterXPaymentWinner>::iterator it = mapMasterXPayeeVotes.begin();
    while(it != mapMasterXPayeeVotes.end()) {
        CMasterXPaymentWinner winner = (*it).second;
        if(winner.nBlockHeight >= nHeight-nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERX_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", MASTERX_SYNC_GMW, nInvCount);
}

std::string CMasterXPayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasterXPayeeVotes.size() <<
            ", Blocks: " << (int)mapMasterXBlocks.size();

    return info.str();
}



int CMasterXPayments::GetOldestBlock()
{
    LOCK(cs_mapMasterXBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMasterXBlockPayees>::iterator it = mapMasterXBlocks.begin();
    while(it != mapMasterXBlocks.end()) {
        if((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}



int CMasterXPayments::GetNewestBlock()
{
    LOCK(cs_mapMasterXBlocks);

    int nNewestBlock = 0;

    std::map<int, CMasterXBlockPayees>::iterator it = mapMasterXBlocks.begin();
    while(it != mapMasterXBlocks.end()) {
        if((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
