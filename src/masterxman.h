// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERXMAN_H
#define MASTERXMAN_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "masterx.h"

#define MASTERXS_DUMP_SECONDS               (15*60)
#define MASTERXS_DSEG_SECONDS               (3*60*60)

using namespace std;

class CMasterXMan;

extern CMasterXMan gmineman;
void DumpMasterXs();

/** Access to the MN database (gmcache.dat)
 */
class CMasterXDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;
public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasterXDB();
    bool Write(const CMasterXMan &gminemanToSave);
    ReadResult Read(CMasterXMan& gminemanToLoad, bool fDryRun = false);
};

class CMasterXMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CMasterX> vMasterXs;
    // who's asked for the MasterX list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasterXList;
    // who we asked for the MasterX list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasterXList;
    // which MasterXs we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMasterXListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CMasterXBroadcast> mapSeenMasterXBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CMasterXPing> mapSeenMasterXPing;
    
    // keep track of dsq count to prevent masterxs from gaming stealthx queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vMasterXs);
        READWRITE(mAskedUsForMasterXList);
        READWRITE(mWeAskedForMasterXList);
        READWRITE(mWeAskedForMasterXListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenMasterXBroadcast);
        READWRITE(mapSeenMasterXPing);
    }

    CMasterXMan();
    CMasterXMan(CMasterXMan& other);

    /// Add an entry
    bool Add(CMasterX &gm);

    /// Ask (source) node for gmb
    void AskForGM(CNode *pnode, CTxIn &vin);

    /// Check all MasterXs
    void Check();

    /// Check all MasterXs and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear MasterX vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CMasterX* Find(const CScript &payee);
    CMasterX* Find(const CTxIn& vin);
    CMasterX* Find(const CPubKey& pubKeyMasterX);

    /// Find an entry in the masterx list that is next to be paid
    CMasterX* GetNextMasterXInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CMasterX* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CMasterX* GetCurrentMasterX(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

    std::vector<CMasterX> GetFullMasterXVector() { Check(); return vMasterXs; }

    std::vector<pair<int, CMasterX> > GetMasterXRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetMasterXRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CMasterX* GetMasterXByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void ProcessMasterXConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) MasterXs
    int size() { return vMasterXs.size(); }

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update masterx list and maps using provided CMasterXBroadcast
    void UpdateMasterXList(CMasterXBroadcast gmb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateMasterXList(CMasterXBroadcast gmb, int& nDos);

};

#endif
