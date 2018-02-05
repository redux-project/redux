// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEMASTERX_H
#define ACTIVEMASTERX_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "init.h"
#include "wallet.h"
#include "stealthx.h"
#include "masterx.h"

#define ACTIVE_MASTERX_INITIAL                     0 // initial state
#define ACTIVE_MASTERX_SYNC_IN_PROCESS             1
#define ACTIVE_MASTERX_INPUT_TOO_NEW               2
#define ACTIVE_MASTERX_NOT_CAPABLE                 3
#define ACTIVE_MASTERX_STARTED                     4

// Responsible for activating the MasterX and pinging the network
class CActiveMasterX
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping MasterX
    bool SendMasterXPing(std::string& errorMessage);

    /// Create MasterX broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyMasterX, CPubKey pubKeyMasterX, std::string &errorMessage, CMasterXBroadcast &gmb);

    /// Get 1000REDUX input that can be used for the MasterX
    bool GetMasterXVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
	// Initialized by init.cpp
	// Keys for the main MasterX
	CPubKey pubKeyMasterX;

	// Initialized while registering MasterX
	CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveMasterX()
    {        
        status = ACTIVE_MASTERX_INITIAL;
    }

    /// Manage status of main MasterX
    void ManageStatus(); 
    std::string GetStatus();

    /// Create MasterX broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CMasterXBroadcast &gmb, bool fOffline = false);

    /// Get 1000REDUX input that can be used for the MasterX
    bool GetMasterXVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsMasterX();

    /// Enable cold wallet mode (run a MasterX with no funds)
    bool EnableHotColdMasterX(CTxIn& vin, CService& addr);
};

#endif
