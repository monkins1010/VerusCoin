/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides support for PBaaS cross chain communication.
 * 
 * In merge mining and notarization, Verus acts as a hub that other PBaaS chains
 * call via RPC in order to get information that allows earning and submitting
 * notarizations.
 * 
 */

#ifndef CROSSCHAINRPC_H
#define CROSSCHAINRPC_H

#include <univalue.h>
#include <sstream>
#include "streams.h"
#include "boost/algorithm/string.hpp"
#include "key_io.h"

static const int DEFAULT_RPC_TIMEOUT=900;
static const uint32_t PBAAS_VERSION = 1;
static const uint32_t PBAAS_VERSION_INVALID = 0;

class CCrossChainRPCData
{
public:
    std::string host;
    int32_t port;
    std::string credentials;

    CCrossChainRPCData() : port(0) {}

    CCrossChainRPCData(std::string Host, int32_t Port, std::string Credentials) :
        host(Host), port(Port), credentials(Credentials) {}
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(host);
        READWRITE(port);        
        READWRITE(credentials);
    }

    static CCrossChainRPCData LoadFromConfig(std::string fPath="");

    static uint160 GetChainID(std::string name);

    static uint160 GetConditionID(uint160 cid, int32_t condition);
    static uint160 GetConditionID(std::string name, int32_t condition);

    UniValue ToUniValue() const;
};

// credentials for now are "user:password"
UniValue RPCCall(const std::string& strMethod, 
                 const UniValue& params, 
                 const std::string credentials="user:pass", 
                 int port=27486, 
                 const std::string host="127.0.0.1", 
                 int timeout=DEFAULT_RPC_TIMEOUT);

UniValue RPCCallRoot(const std::string& strMethod, const UniValue& params, int timeout=DEFAULT_RPC_TIMEOUT);

template <typename SERIALIZABLE>
std::vector<unsigned char> AsVector(const SERIALIZABLE &obj)
{
    CDataStream s = CDataStream(SER_NETWORK, PROTOCOL_VERSION);
    s << obj;
    return std::vector<unsigned char>(s.begin(), s.end());
}

template <typename SERIALIZABLE>
void FromVector(const std::vector<unsigned char> &vch, SERIALIZABLE &obj)
{
    CDataStream s(vch, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        obj.Unserialize(s);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
}

template <typename SERIALIZABLE>
uint256 GetHash(const SERIALIZABLE &obj)
{
    CHashWriter hw(SER_GETHASH, PROTOCOL_VERSION);
    hw << obj;
    return hw.GetHash();
}

bool uni_get_bool(UniValue uv, bool def=false);
int32_t uni_get_int(UniValue uv, int32_t def=0);
int64_t uni_get_int64(UniValue uv, int64_t def =0);
std::string uni_get_str(UniValue uv, std::string def="");
std::vector<UniValue> uni_getValues(UniValue uv, std::vector<UniValue> def=std::vector<UniValue>());

class CNodeData
{
public:
    std::string networkAddress;
    CKeyID paymentAddress;

    CNodeData() {}
    CNodeData(UniValue &);
    CNodeData(std::string netAddr, uint160 paymentKeyID) : networkAddress(netAddr), paymentAddress(paymentKeyID) {}
    CNodeData(std::string netAddr, std::string paymentAddr) :
        networkAddress(netAddr)
    {
        paymentAddress = GetDestinationID(CTxDestination(paymentAddr));
    }
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(networkAddress);
        READWRITE(paymentAddress);        
    }

    UniValue ToUniValue() const;
};

// This defines the currency characteristics of a PBaaS currency that will be the native coins of a PBaaS chain
class CPBaaSChainDefinition
{
public:
    static const int64_t MIN_PER_BLOCK_NOTARIZATION = 1000000;  // 0.01 VRSC per block notarization minimum
    static const int64_t MIN_BILLING_PERIOD = 480;  // 8 hour minimum billing period for notarization, typically expect days/weeks/months
    static const int64_t DEFAULT_OUTPUT_VALUE = 100000;  // 0.001 VRSC default output value

    static const uint32_t OPTION_RESERVE = 1;       // allows reserve conversion using base calculations when set
    static const uint32_t OPTION_ID_ISSUANCE = 2;   // clear is permissionless, if set, IDs may only be created by controlling ID
    static const uint32_t OPTION_ID_STAKING = 4;    // all IDs on chain stake equally, rather than value-based staking
    static const uint32_t OPTION_ID_REFERRALS = 8;  // if set, this chain supports referrals

    uint32_t nVersion;                      // version of this chain definition data structure to allow for extensions (not daemon version)
    std::string name;                       // chain name - specifies the ID

    CKeyID address;                         // non-purchased/converted premine and fee recipient address

    int64_t premine;                        // initial supply that is distributed to the premine output address, but not purchased
    int64_t initialcontribution;            // optional initial contribution by this transaction. this serves to ensure the option to participate by whoever defines the chain
    int64_t minpreconvert;                  // zero for now, later can be used for Kickstarter-like launch and return all non-network fees upon failure to meet minimum
    int64_t maxpreconvert;                  // maximum amount of reserve that can be pre-converted
    int64_t preconverted;                   // actual converted amount if known
    int64_t conversion;                     // initial reserve ratio, also value in Verus if premine is 0, otherwise value is conversion * maxpreconvert/(maxpreconvert + premine)
    int64_t launchFee;                      // fee deducted from reserve purchase before conversion. always 100000000 (100%) for non-reserve currencies
    int32_t startBlock;                     // parent chain block # that must kickoff the notarization of block 0, cannot be before this block
    int32_t endBlock;                       // block after which this is considered end-of-lifed

    // TODO:PBAAS remove this member, which is no longer used - left for compatibility with testnet while completing IDs
    int32_t eras;                           // number of eras, each vector below should have an entry for each

    std::vector<int64_t> rewards;           // initial reward in each ERA in native coin
    std::vector<int64_t> rewardsDecay;      // decay of rewards during the era - only applies to non-converted values
    std::vector<int32_t> halving;           // number of blocks between halvings
    std::vector<int32_t> eraEnd;            // block number that ends each ERA

    std::vector<int32_t> eraOptions;        // flags to determine fungibility and conversion for each ERA

    int32_t billingPeriod;                  // number of blocks in one billing period
    int64_t notarizationReward;             // default amount per block for notarizations

    std::vector<CNodeData> nodes;           // network nodes

    CPBaaSChainDefinition() : nVersion(PBAAS_VERSION_INVALID) {}

    CPBaaSChainDefinition(const UniValue &obj);

    CPBaaSChainDefinition(const std::vector<unsigned char> &asVector)
    {
        ::FromVector(asVector, *this);
    }

    CPBaaSChainDefinition(const CTransaction &tx, bool validate = false);

    CPBaaSChainDefinition(std::string Name, std::string Address, int64_t Premine, int64_t initialContribution,
                          int64_t Conversion, int64_t minPreConvert, int64_t maxPreConvert, int64_t preConverted, int64_t LaunchFee,
                          int32_t StartBlock, int32_t EndBlock, int32_t chainEras,
                          const std::vector<int64_t> &chainRewards, const std::vector<int64_t> &chainRewardsDecay,
                          const std::vector<int32_t> &chainHalving, const std::vector<int32_t> &chainEraEnd, std::vector<int32_t> &chainCurrencyOptions,
                          int32_t BillingPeriod, int64_t NotaryReward, std::vector<CNodeData> &Nodes) :
                            nVersion(PBAAS_VERSION),
                            name(Name),
                            premine(Premine),
                            initialcontribution(initialContribution),
                            conversion(Conversion),
                            minpreconvert(minPreConvert),
                            maxpreconvert(maxPreConvert),
                            preconverted(preConverted),
                            launchFee(LaunchFee),
                            startBlock(StartBlock),
                            endBlock(EndBlock),
                            eras(chainEras),
                            rewards(chainRewards),
                            rewardsDecay(chainRewardsDecay),
                            halving(chainHalving),
                            eraEnd(chainEraEnd),
                            eraOptions(chainCurrencyOptions),
                            billingPeriod(BillingPeriod),
                            notarizationReward(NotaryReward),
                            nodes(Nodes)
    {
        if (Name.size() > (KOMODO_ASSETCHAIN_MAXLEN - 1))
        {
            Name.resize(KOMODO_ASSETCHAIN_MAXLEN - 1);
        }
        name = Name;
        address = GetDestinationID(CTxDestination(Address));
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(name);        
        READWRITE(address);        
        READWRITE(VARINT(premine));
        READWRITE(VARINT(initialcontribution));
        READWRITE(VARINT(conversion));
        READWRITE(VARINT(minpreconvert));
        READWRITE(VARINT(maxpreconvert));
        READWRITE(VARINT(preconverted));
        READWRITE(VARINT(launchFee));
        READWRITE(startBlock);
        READWRITE(endBlock);
        READWRITE(eras);
        READWRITE(rewards);
        READWRITE(rewardsDecay);
        READWRITE(halving);
        READWRITE(eraEnd);
        READWRITE(eraOptions);
        READWRITE(billingPeriod);
        READWRITE(VARINT(notarizationReward));
        READWRITE(nodes);
    }

    std::vector<unsigned char> AsVector() const
    {
        return ::AsVector(*this);
    }

    static uint160 GetChainID(std::string name);

    uint160 GetChainID() const
    {
        return GetChainID(name);
    }

    uint160 GetConditionID(int32_t condition) const;

    bool IsValid() const
    {
        return (nVersion != PBAAS_VERSION_INVALID) && name.size() > 0 && name.size() <= (KOMODO_ASSETCHAIN_MAXLEN - 1) && rewards.size() > 0 && (rewards.size() <= ASSETCHAINS_MAX_ERAS);
    }

    int32_t ChainOptions() const
    {
        return eraOptions.size() ? eraOptions[0] : 0;
    }

    UniValue ToUniValue() const;

    int GetDefinedPort() const;

    bool IsReserve() const
    {
        return ChainOptions() & OPTION_RESERVE;
    }

    bool IDRequiresPermission() const
    {
        return ChainOptions() & OPTION_ID_ISSUANCE;
    }

    bool IDStaking() const
    {
        return ChainOptions() & OPTION_ID_STAKING;
    }

    bool IDReferrals() const
    {
        return ChainOptions() & OPTION_ID_REFERRALS;
    }
};

#endif
