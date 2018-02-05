

// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERX_PAYMENTS_H
#define MASTERX_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "masterx.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapMasterXBlocks;
extern CCriticalSection cs_mapMasterXPayeeVotes;

class CMasterXPayments;
class CMasterXPaymentWinner;
class CMasterXBlockPayees;

extern CMasterXPayments masterxPayments;

#define GMPAYMENTS_SIGNATURES_REQUIRED           6
#define GMPAYMENTS_SIGNATURES_TOTAL              10

void ProcessMessageMasterXPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsReferenceNode(CTxIn& vin);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue);
void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);

void DumpMasterXPayments();

/** Save MasterX Payment Data (gmpayments.dat)
 */
class CMasterXPaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CMasterXPaymentDB();
    bool Write(const CMasterXPayments &objToSave);
    ReadResult Read(CMasterXPayments& objToLoad, bool fDryRun = false);
};

class CMasterXPayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CMasterXPayee() {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CMasterXPayee(CScript payee, int nVotesIn) {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
     }
};

// Keep track of votes for payees from masterxs
class CMasterXBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasterXPayee> vecPayments;

    CMasterXBlockPayees(){
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CMasterXBlockPayees(int nBlockHeightIn) {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement){
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CMasterXPayee& payee, vecPayments){
            if(payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CMasterXPayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH(CMasterXPayee& p, vecPayments){
            if(p.nVotes > nVotes){
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CMasterXPayee& p, vecPayments){
            if(p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
     }
};

// for storing the winning payments
class CMasterXPaymentWinner
{
public:
    CTxIn vinMasterX;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CMasterXPaymentWinner() {
        nBlockHeight = 0;
        vinMasterX = CTxIn();
        payee = CScript();
    }

    CMasterXPaymentWinner(CTxIn vinIn) {
        nBlockHeight = 0;
        vinMasterX = vinIn;
        payee = CScript();
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinMasterX.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyMasterX, CPubKey& pubKeyMasterX);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn){
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinMasterX);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinMasterX.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// MasterX Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasterXPayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CMasterXPaymentWinner> mapMasterXPayeeVotes;
    std::map<int, CMasterXBlockPayees> mapMasterXBlocks;
    std::map<uint256, int> mapMasterXLastVote; //prevout.hash + prevout.n, nBlockHeight

    CMasterXPayments() {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear() {
        LOCK2(cs_mapMasterXBlocks, cs_mapMasterXPayeeVotes);
        mapMasterXBlocks.clear();
        mapMasterXPayeeVotes.clear();
    }

    bool AddWinningMasterX(CMasterXPaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CMasterX& gm);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CMasterX& gm, int nNotBlockHeight);

    bool CanVote(COutPoint outMasterX, int nBlockHeight) {
        LOCK(cs_mapMasterXPayeeVotes);

        if(mapMasterXLastVote.count(outMasterX.hash + outMasterX.n)) {
            if(mapMasterXLastVote[outMasterX.hash + outMasterX.n] == nBlockHeight) {
                return false;
            }
        }

        //record this masterx voted
        mapMasterXLastVote[outMasterX.hash + outMasterX.n] = nBlockHeight;
        return true;
    }

    int GetMinMasterXPaymentsProto();
    void ProcessMessageMasterXPayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapMasterXPayeeVotes);
        READWRITE(mapMasterXBlocks);
    }
};



#endif
