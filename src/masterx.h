
// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERX_H
#define MASTERX_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "timedata.h"

#define MASTERX_MIN_CONFIRMATIONS           15
#define MASTERX_MIN_MNP_SECONDS             (10*60)
#define MASTERX_MIN_MNB_SECONDS             (5*60)
#define MASTERX_PING_SECONDS                (5*60)
#define MASTERX_EXPIRATION_SECONDS          (65*60)
#define MASTERX_REMOVAL_SECONDS             (75*60)
#define MASTERX_CHECK_SECONDS               5

using namespace std;

class CMasterX;
class CMasterXBroadcast;
class CMasterXPing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The MasterX Ping Class : Contains a different serialize method for sending pings from masterxs throughout the network
//

class CMasterXPing
{
public:

    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //gmb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CMasterXPing();
    CMasterXPing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(CKey& keyMasterX, CPubKey& pubKeyMasterX);
    bool VerifySignature(CPubKey& pubKeyMasterX, int &nDos);
    void Relay();

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CMasterXPing& first, CMasterXPing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CMasterXPing& operator=(CMasterXPing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasterXPing& a, const CMasterXPing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CMasterXPing& a, const CMasterXPing& b)
    {
        return !(a == b);
    }

};


//
// The MasterX Class. For managing the StealthX process. It contains the input of the 1000REDUX, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasterX
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;
public:
    enum state {
        MASTERX_ENABLED = 1,
        MASTERX_EXPIRED = 2,
        MASTERX_VIN_SPENT = 3,
        MASTERX_REMOVE = 4,
        MASTERX_POS_ERROR = 5
    };

    CTxIn vin;
    CService addr;
    CPubKey pubkey;
    CPubKey pubkey2;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //gmb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CMasterXPing lastPing;

    CMasterX();
    CMasterX(const CMasterX& other);
    CMasterX(const CMasterXBroadcast& gmb);


    void swap(CMasterX& first, CMasterX& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubkey, second.pubkey);
        swap(first.pubkey2, second.pubkey2);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CMasterX& operator=(CMasterX from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasterX& a, const CMasterX& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMasterX& a, const CMasterX& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod=1, int64_t nBlockHeight=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
            LOCK(cs);

            READWRITE(vin);
            READWRITE(addr);
            READWRITE(pubkey);
            READWRITE(pubkey2);
            READWRITE(sig);
            READWRITE(sigTime);
            READWRITE(protocolVersion);
            READWRITE(activeState);
            READWRITE(lastPing);
            READWRITE(cacheInputAge);
            READWRITE(cacheInputAgeBlock);
            READWRITE(unitTest);
            READWRITE(allowFreeTx);
            READWRITE(nLastDsq);
            READWRITE(nScanningErrorCount);
            READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CMasterXBroadcast& gmb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash+slice*64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CMasterXPing())
                ? false
                : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CMasterXPing();
    }

    bool IsEnabled()
    {
        return activeState == MASTERX_ENABLED;
    }

    int GetMasterXInputAge()
    {
        if(chainActive.Tip() == NULL) return 0;

        if(cacheInputAge == 0){
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge+(chainActive.Tip()->nHeight-cacheInputAgeBlock);
    }

    std::string Status() {
        std::string strStatus = "ACTIVE";

        if(activeState == CMasterX::MASTERX_ENABLED) strStatus   = "ENABLED";
        if(activeState == CMasterX::MASTERX_EXPIRED) strStatus   = "EXPIRED";
        if(activeState == CMasterX::MASTERX_VIN_SPENT) strStatus = "VIN_SPENT";
        if(activeState == CMasterX::MASTERX_REMOVE) strStatus    = "REMOVE";
        if(activeState == CMasterX::MASTERX_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();

};


//
// The MasterX Broadcast Class : Contains a different serialize method for sending masterxs through the network
//

class CMasterXBroadcast : public CMasterX
{
public:
    CMasterXBroadcast();
    CMasterXBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CMasterXBroadcast(const CMasterX& gm);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    bool VerifySignature();
    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubkey);
        READWRITE(pubkey2);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubkey;
        return ss.GetHash();
    }

};

#endif
