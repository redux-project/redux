// Copyright (c) 2015-2016 The Redux developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERX_SYNC_H
#define MASTERX_SYNC_H

#define MASTERX_SYNC_INITIAL           0
#define MASTERX_SYNC_SPORKS            1
#define MASTERX_SYNC_LIST              2
#define MASTERX_SYNC_GMW               3
#define MASTERX_SYNC_EVOLUTION            4
#define MASTERX_SYNC_EVOLUTION_PROP       10
#define MASTERX_SYNC_EVOLUTION_FIN        11
#define MASTERX_SYNC_FAILED            998
#define MASTERX_SYNC_FINISHED          999

#define MASTERX_SYNC_TIMEOUT           5
#define MASTERX_SYNC_THRESHOLD         2

class CMasterXSync;
extern CMasterXSync masterxSync;

//
// CMasterXSync : Sync masterx assets in stages
//

class CMasterXSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncGMW;
    std::map<uint256, int> mapSeenSyncEvolution;

    int64_t lastMasterXList;
    int64_t lastMasterXWinner;
    int64_t lastEvolutionItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumMasterXList;
    int sumMasterXWinner;
    int sumEvolutionItemProp;
    int sumEvolutionItemFin;
    // peers that reported counts
    int countMasterXList;
    int countMasterXWinner;
    int countEvolutionItemProp;
    int countEvolutionItemFin;

    // Count peers we've requested the list from
    int RequestedMasterXAssets;
    int RequestedMasterXAttempt;

    // Time when current masterx asset sync started
    int64_t nAssetSyncStarted;

    CMasterXSync();

    void AddedMasterXList(uint256 hash);
    void AddedMasterXWinner(uint256 hash);
    void AddedEvolutionItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsEvolutionFinEmpty();
    bool IsEvolutionPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    void ClearFulfilledRequest();
};

#endif
