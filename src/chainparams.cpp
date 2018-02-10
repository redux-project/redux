// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2016 The Redux developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

static Checkpoints::MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (    0, uint256("0x00000a3a8fa7380d280be066f7ecf4ba0c650839034964db5a6143fb11eafdbf"))
	
        
        ;
static const Checkpoints::CCheckpointData data = {
        &mapCheckpoints,
        1517795594, // * UNIX timestamp of last checkpoint block
        0,      // * total number of transactions between genesis and last checkpoint
                    //   (the tx=... number in the SetBestChain debug.log lines)
        //2800        // * estimated number of transactions per day after checkpoint
    };

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, uint256("0x000007bac4e31c81e5aa58b605cb2064be9bdc77ee95879f07e31d83bf6b8787"))
        ;
static const Checkpoints::CCheckpointData dataTestnet = {
        &mapCheckpointsTestnet,
        1517795647,
        0,
        //0
    };

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
        boost::assign::map_list_of
        ( 0, uint256("0x58ec34fd3ef67f14590a3872e7ea24aeb49e8adfafab9ddde8d3de62181da17a"))
        ;
static const Checkpoints::CCheckpointData dataRegtest = {
        &mapCheckpointsRegtest,
        1517795725,
        0,
        //0
    };

class CMainParams : public CChainParams {
public:
    CMainParams() {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";
        /** 
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x3f;
        pchMessageStart[1] = 0xb2;
        pchMessageStart[2] = 0x2c;
		pchMessageStart[3] = 0x62;
        
		vAlertPubKey = ParseHex("049fd428490b19ad255de37bfcc2934f926af304aa0a9c57cabcabeb0d72397ec8badec10e065341e922bf0bdc5846c96d985f2caaf7db33b587c8b030b8249c51");
        nDefaultPort = 9667;
        bnProofOfWorkLimit = ~uint256(0) >> 20;  // Redux starting difficulty is 1 / 2^12
        nSubsidyHalvingInterval = 210000;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Redux: 1 day
        nTargetSpacing = 2.5 * 60; // Redux: 2.5 minutes

       
        const char* pszTimestamp = "USAToday 2/5/2018 Wells Fargo: Fed order could cut $400M from bank's profit this year";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 50 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("044120661eb71b0b0da8b0a38074c52f259a9503bd0ada0e5716bb63eca4379c4f4d09c94234dbecaf7aac38c825dc70ffea92eb747650c580f75307feb0bccd76") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1517795594;
        genesis.nBits    = 0x1e0ffff0;
        genesis.nNonce   = 1456611;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x00000a3a8fa7380d280be066f7ecf4ba0c650839034964db5a6143fb11eafdbf"));
        assert(genesis.hashMerkleRoot == uint256("0xbf22bef8da3272017d49c7a14a90b3a0d54cc0f83cd73eee3d702cbb16d2c790"));

        vSeeds.push_back(CDNSSeedData("159.65.20.209", "159.65.20.209"));
	vSeeds.push_back(CDNSSeedData("1172.31.47.219", "172.31.47.219"));
	vSeeds.push_back(CDNSSeedData("165.227.226.176", "165.227.226.176"));
	

        base58Prefixes[PUBKEY_ADDRESS] = list_of( 76);                    // Redux addresses start with 'X'
        base58Prefixes[SCRIPT_ADDRESS] = list_of(  8);                    // Redux script addresses start with '4'
        base58Prefixes[SECRET_KEY] =     list_of(176);                    // Redux private keys start with '7' or 'X'
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x07)(0xE8)(0xF8)(0x9C); // Redux BIP32 pubkeys
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x07)(0x74)(0xA1)(0x37); // Redux BIP32 prvkeys
        base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000005);             // Redux BIP44 coin type is '5'

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        strSporkKey = "045ca3f7afdede008169e8d762566b0cc226e81855be5b748b62b4007e371f5d0d6dae49b97fe4ebe1d0ea69a3d98aa906c2d5ff9a9b6dd5beead62bfff05370c0";
        strMasterXPaymentsPubKey = "045ca3f7afdede008169e8d762566b0cc226e81855be5b748b62b4007e371f5d0d6dae49b97fe4ebe1d0ea69a3d98aa906c2d5ff9a9b6dd5beead62bfff05370c0";
        strStealthXPoolDummyAddress = "XbJNRzf2fu1tUi2cPWMan5d5hZ3o8swAT3";
        nStartMasterXPayments = 1517795594; //02.04.2018 @ 08:53:14 EST (PST -03:00)
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0x8d;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0x9b;
        pchMessageStart[3] = 0x1a;
        vAlertPubKey = ParseHex("04800fe5539803e6712914ce8f546336fb0735c10d95058b2dca84513f732a659ffa3c8865b432e5a4388d752425fe96492350fb4e9940c1266cb0a0635f8738fe");
        nDefaultPort = 19667;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Redux: 1 day
        nTargetSpacing = 2.5 * 60; // Redux: 2.5 minutes

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1517795647;
        genesis.nNonce = 277888;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x000007bac4e31c81e5aa58b605cb2064be9bdc77ee95879f07e31d83bf6b8787"));

        vFixedSeeds.clear();
        vSeeds.clear();
        
        base58Prefixes[PUBKEY_ADDRESS] = list_of(83);                    // Testnet redux addresses start with 'a' 
        base58Prefixes[SCRIPT_ADDRESS] = list_of( 9);                    // Testnet redux script addresses start with '4' or '5'
        base58Prefixes[SECRET_KEY]     = list_of(239);                    // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x09)(0x72)(0x98)(0xBF); // Testnet redux BIP32 pubkeys 
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x09)(0x62)(0x3A)(0x6F); // Testnet redux BIP32 prvkeys
        base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000001);             // Testnet redux BIP44 coin type is '5' (All coin's testnet default)

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "040e5d6c1e2268daec7d170aa4f72e077cebd88e9a171a03143e6a031c1f5dc48d81f908c0d711d46e4176afe30f6f3c1ffb5bdbf3e6478b925adc5909d667ecbc";
        strMasterXPaymentsPubKey = "040e5d6c1e2268daec7d170aa4f72e077cebd88e9a171a03143e6a031c1f5dc48d81f908c0d711d46e4176afe30f6f3c1ffb5bdbf3e6478b925adc5909d667ecbc";
        strStealthXPoolDummyAddress = "XeZBmkcHw3DyBJvfGn3TQ8kYVoPRRzaT4f";
        nStartMasterXPayments = 1517795647; //02.04.2018 @ 08:54:07 EST (PST -03:00)
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        pchMessageStart[0] = 0x3a;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0x3c;
        pchMessageStart[3] = 0x3d;
        nSubsidyHalvingInterval = 150;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 24 * 60 * 60; // Redux: 1 day
        nTargetSpacing = 2.5 * 60; // Redux: 2.5 minutes
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = 1517795725;
        genesis.nBits = 0x207fffff;
        genesis.nNonce = 0;

        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 19669;
        assert(hashGenesisBlock == uint256("0x58ec34fd3ef67f14590a3872e7ea24aeb49e8adfafab9ddde8d3de62181da17a"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataRegtest;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams {
public:
    CUnitTestParams() {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 17334;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval)  { nSubsidyHalvingInterval=anSubsidyHalvingInterval; }
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority)  { nEnforceBlockUpgradeMajority=anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority)  { nRejectBlockOutdatedMajority=anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority)  { nToCheckBlockUpgradeMajority=anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks)  { fDefaultConsistencyChecks=afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) {  fAllowMinDifficultyBlocks=afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams *pCurrentParams = 0;

CModifiableParams *ModifiableParams()
{
   assert(pCurrentParams);
   assert(pCurrentParams==&unitTestParams);
   return (CModifiableParams*)&unitTestParams;
}

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        case CBaseChainParams::UNITTEST:
            return unitTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
