
#include "addrman.h"
#include "protocol.h"
#include "activemasterx.h"
#include "masterxman.h"
#include "masterx.h"
#include "masterxconfig.h"
#include "spork.h"

//
// Bootup the MasterX, look for a 1000REDUX input and register on the network
//
void CActiveMasterX::ManageStatus()
{    
    std::string errorMessage;

    if(!fMasterX) return;

    if (fDebug) LogPrintf("CActiveMasterX::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if(Params().NetworkID() != CBaseChainParams::REGTEST && !masterxSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERX_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasterX::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if(status == ACTIVE_MASTERX_SYNC_IN_PROCESS) status = ACTIVE_MASTERX_INITIAL;

    if(status == ACTIVE_MASTERX_INITIAL) {
        CMasterX *pgm;
        pgm = gmineman.Find(pubKeyMasterX);
        if(pgm != NULL) {
            pgm->Check();
            if(pgm->IsEnabled() && pgm->protocolVersion == PROTOCOL_VERSION) EnableHotColdMasterX(pgm->vin, pgm->addr);
        }
    }

    if(status != ACTIVE_MASTERX_STARTED) {

        // Set defaults
        status = ACTIVE_MASTERX_NOT_CAPABLE;
        notCapableReason = "";

        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(pwalletMain->GetBalance() == 0){
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(strMasterXAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masterxaddr configuration option.";
                LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterXAddr);
        }

        if(Params().NetworkID() == CBaseChainParams::MAIN) {
            if(service.GetPort() != 9667) {
                notCapableReason = strprintf("Invalid port: %u - only 9667 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if(service.GetPort() == 9667) {
            notCapableReason = strprintf("Invalid port: %u - 9667 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveMasterX::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode *pnode = ConnectNode((CAddress)service, NULL, false);
        if(!pnode){
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasterX::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetMasterXVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            if(GetInputAge(vin) < MASTERX_MIN_CONFIRMATIONS){
                status = ACTIVE_MASTERX_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveMasterX::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyMasterX;
            CKey keyMasterX;

            if(!spySendSigner.SetKey(strMasterXPrivKey, errorMessage, keyMasterX, pubKeyMasterX))
            {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CMasterXBroadcast gmb;
            if(!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMasterX, pubKeyMasterX, errorMessage, gmb)) {
                notCapableReason = "Error on CreateBroadcast: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveMasterX::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            gmb.Relay();

            LogPrintf("CActiveMasterX::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_MASTERX_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveMasterX::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if(!SendMasterXPing(errorMessage)) {
        LogPrintf("CActiveMasterX::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveMasterX::GetStatus() {
    switch (status) {
    case ACTIVE_MASTERX_INITIAL: return "Node just started, not yet activated";
    case ACTIVE_MASTERX_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start MasterX";
    case ACTIVE_MASTERX_INPUT_TOO_NEW: return strprintf("MasterX input must have at least %d confirmations", MASTERX_MIN_CONFIRMATIONS);
    case ACTIVE_MASTERX_NOT_CAPABLE: return "Not capable masterx: " + notCapableReason;
    case ACTIVE_MASTERX_STARTED: return "MasterX successfully started";
    default: return "unknown";
    }
}

bool CActiveMasterX::SendMasterXPing(std::string& errorMessage) {
    if(status != ACTIVE_MASTERX_STARTED) {
        errorMessage = "MasterX is not in a running status";
        return false;
    }

    CPubKey pubKeyMasterX;
    CKey keyMasterX;

    if(!spySendSigner.SetKey(strMasterXPrivKey, errorMessage, keyMasterX, pubKeyMasterX))
    {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveMasterX::SendMasterXPing() - Relay MasterX Ping vin = %s\n", vin.ToString());
    
    CMasterXPing mnp(vin);
    if(!mnp.Sign(keyMasterX, pubKeyMasterX))
    {
        errorMessage = "Couldn't sign MasterX Ping";
        return false;
    }

    // Update lastPing for our masterx in MasterX list
    CMasterX* pgm = gmineman.Find(vin);
    if(pgm != NULL)
    {
        if(pgm->IsPingedWithin(MASTERX_PING_SECONDS, mnp.sigTime)){
            errorMessage = "Too early to send MasterX Ping";
            return false;
        }

        pgm->lastPing = mnp;
        gmineman.mapSeenMasterXPing.insert(make_pair(mnp.GetHash(), mnp));

        //gmineman.mapSeenMasterXBroadcast.lastPing is probably outdated, so we'll update it
        CMasterXBroadcast gmb(*pgm);
        uint256 hash = gmb.GetHash();
        if(gmineman.mapSeenMasterXBroadcast.count(hash)) gmineman.mapSeenMasterXBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    }
    else
    {
        // Seems like we are trying to send a ping while the MasterX is not registered in the network
        errorMessage = "StealthX MasterX List doesn't include our MasterX, shutting down MasterX pinging service! " + vin.ToString();
        status = ACTIVE_MASTERX_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }

}

bool CActiveMasterX::CreateBroadcast(std::string strService, std::string strKeyMasterX, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CMasterXBroadcast &gmb, bool fOffline) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyMasterX;
    CKey keyMasterX;

    //need correct blocks to send ping
    if(!fOffline && !masterxSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start MasterX";
        LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.SetKey(strKeyMasterX, errorMessage, keyMasterX, pubKeyMasterX))
    {
        errorMessage = strprintf("Can't find keys for masterx %s - %s", strService, errorMessage);
        LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!GetMasterXVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for masterx %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);
    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(service.GetPort() != 9667) {
            errorMessage = strprintf("Invalid port %u for masterx %s - only 9667 is supported on mainnet.", service.GetPort(), strService);
            LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
            return false;
        }
    } else if(service.GetPort() == 9667) {
        errorMessage = strprintf("Invalid port %u for masterx %s - 9667 is only supported on mainnet.", service.GetPort(), strService);
        LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2*60*60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyMasterX, pubKeyMasterX, errorMessage, gmb);
}

bool CActiveMasterX::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyMasterX, CPubKey pubKeyMasterX, std::string &errorMessage, CMasterXBroadcast &gmb) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CMasterXPing mnp(vin);
    if(!mnp.Sign(keyMasterX, pubKeyMasterX)){
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveMasterX::CreateBroadcast() -  %s\n", errorMessage);
        gmb = CMasterXBroadcast();
        return false;
    }

    gmb = CMasterXBroadcast(service, vin, pubKeyCollateralAddress, pubKeyMasterX, PROTOCOL_VERSION);
    gmb.lastPing = mnp;
    if(!gmb.Sign(keyCollateralAddress)){
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveMasterX::CreateBroadcast() - %s\n", errorMessage);
        gmb = CMasterXBroadcast();
        return false;
    }

    return true;
}

bool CActiveMasterX::GetMasterXVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    return GetMasterXVin(vin, pubkey, secretKey, "", "");
}

bool CActiveMasterX::GetMasterXVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if(!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsMasterX();
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = atoi(strOutputIndex.c_str());
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            LogPrintf("CActiveMasterX::GetMasterXVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveMasterX::GetMasterXVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract MasterX vin information from output
bool CActiveMasterX::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveMasterX::GetMasterXVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf ("CActiveMasterX::GetMasterXVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running MasterX
vector<COutput> CActiveMasterX::SelectCoinsMasterX()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from masterx.conf
    if(GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH(CMasterXConfig::CMasterXEntry mne, masterxConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, atoi(mne.getOutputIndex().c_str()));
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from masterx.conf back if they where temporary unlocked
    if(!confLockedCoins.empty()) {
        BOOST_FOREACH(COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == 1000*COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a MasterX, this can enable to run as a hot wallet with no funds
bool CActiveMasterX::EnableHotColdMasterX(CTxIn& newVin, CService& newService)
{
    if(!fMasterX) return false;

    status = ACTIVE_MASTERX_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasterX::EnableHotColdMasterX() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
