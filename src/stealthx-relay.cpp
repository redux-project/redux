
#include "stealthx-relay.h"


CStealthXRelay::CStealthXRelay()
{
    vinMasterX = CTxIn();
    nBlockHeight = 0;
    nRelayType = 0;
    in = CTxIn();
    out = CTxOut();
}

CStealthXRelay::CStealthXRelay(CTxIn& vinMasterXIn, vector<unsigned char>& vchSigIn, int nBlockHeightIn, int nRelayTypeIn, CTxIn& in2, CTxOut& out2)
{
    vinMasterX = vinMasterXIn;
    vchSig = vchSigIn;
    nBlockHeight = nBlockHeightIn;
    nRelayType = nRelayTypeIn;
    in = in2;
    out = out2;
}

std::string CStealthXRelay::ToString()
{
    std::ostringstream info;

    info << "vin: " << vinMasterX.ToString() <<
        " nBlockHeight: " << (int)nBlockHeight <<
        " nRelayType: "  << (int)nRelayType <<
        " in " << in.ToString() <<
        " out " << out.ToString();
        
    return info.str();   
}

bool CStealthXRelay::Sign(std::string strSharedKey)
{
    std::string strMessage = in.ToString() + out.ToString();

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!spySendSigner.SetKey(strSharedKey, errorMessage, key2, pubkey2))
    {
        LogPrintf("CStealthXRelay():Sign - ERROR: Invalid shared key: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig2, key2)) {
        LogPrintf("CStealthXRelay():Sign - Sign message failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubkey2, vchSig2, strMessage, errorMessage)) {
        LogPrintf("CStealthXRelay():Sign - Verify message failed\n");
        return false;
    }

    return true;
}

bool CStealthXRelay::VerifyMessage(std::string strSharedKey)
{
    std::string strMessage = in.ToString() + out.ToString();

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!spySendSigner.SetKey(strSharedKey, errorMessage, key2, pubkey2))
    {
        LogPrintf("CStealthXRelay()::VerifyMessage - ERROR: Invalid shared key: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubkey2, vchSig2, strMessage, errorMessage)) {
        LogPrintf("CStealthXRelay()::VerifyMessage - Verify message failed\n");
        return false;
    }

    return true;
}

void CStealthXRelay::Relay()
{
    int nCount = std::min(gmineman.CountEnabled(MIN_POOL_PEER_PROTO_VERSION), 20);
    int nRank1 = (rand() % nCount)+1; 
    int nRank2 = (rand() % nCount)+1; 

    //keep picking another second number till we get one that doesn't match
    while(nRank1 == nRank2) nRank2 = (rand() % nCount)+1;

    //printf("rank 1 - rank2 %d %d \n", nRank1, nRank2);

    //relay this message through 2 separate nodes for redundancy
    RelayThroughNode(nRank1);
    RelayThroughNode(nRank2);
}

void CStealthXRelay::RelayThroughNode(int nRank)
{
    CMasterX* pgm = gmineman.GetMasterXByRank(nRank, nBlockHeight, MIN_POOL_PEER_PROTO_VERSION);

    if(pgm != NULL){
        //printf("RelayThroughNode %s\n", pgm->addr.ToString().c_str());
        CNode* pnode = ConnectNode((CAddress)pgm->addr, NULL, false);
        if(pnode){
            //printf("Connected\n");
            pnode->PushMessage("dsr", (*this));
            pnode->Release();
            return;
        }
    } else {
        //printf("RelayThroughNode NULL\n");
    }
}
