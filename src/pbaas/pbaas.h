/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides support for PBaaS initialization, notarization, and cross-chain token
 * transactions and enabling liquid or non-liquid tokens across the
 * Verus ecosystem.
 * 
 * 
 */

#ifndef PBAAS_H
#define PBAAS_H

#include <vector>

#include "cc/CCinclude.h"
#include "streams.h"
#include "script/script.h"
#include "amount.h"
#include "pbaas/crosschainrpc.h"
#include "pbaas/reserves.h"
#include "mmr.h"

#include <boost/algorithm/string.hpp>

extern CAmount AmountFromValue(const UniValue& value);
extern UniValue ValueFromAmount(const CAmount& amount);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex, bool fIncludeAsm=true);

class CPBaaSNotarization;

// these are output cryptoconditions for the Verus reserve liquidity system
// VRSC can be proxied to other PBaaS chains and sent back for use with this system
// The assumption that Verus is either the proxy on the PBaaS chain or the native
// coin on Verus enables us to reduce data requirements systemwide

// this is for transaction outputs with a Verus proxy on a PBaaS chain cryptoconditions with these outputs
// must also be funded with the native chain for fees, unless the chain is a Verus reserve chain, in which
// case the fee will be autoconverted from the Verus proxy through the conversion rules of this chain

// bits for currency controls
// if CURRENCY_FUNGIBLE is set, it can be bought and sold automatically through its reserves, and any issuance
// must be backed by some amount of Verus. issuance can include inflation of supply relative to reserves, but
// a reserve over 100% can be used to ensure that inflation never causes the reserve to drop below 100% if that
// is desired.
// If CURRENCY_RESERVE is set, then it can at least be converted from the currency to Verus in order to
// pay rewards in Verus. If the currency is reserve, but not fractional and not fungible, all rewards will
// be automatically converted to Verus for payout, and if reserves are emptied, the chain will stop running unless
// miners and stakers decide to mine and stake it for free.
// If CURRENCY_FRACTIONAL is set, then at 100% or less, the Bancor math formulas will determine conversion
// rates at each mined block, dependent on how many exchanges in either direction are processed in that
// block. All transactions processed get exactly the same price for both exchange to and echange from in any
// given block.
// if CURRENCY_CONVERTSTAKES is set, even stake rewards are required to be automatically converted to reserves.
//  

static const uint32_t PBAAS_NODESPERNOTARIZATION = 2;       // number of nodes to reference in each notarization
static const int64_t PBAAS_MINNOTARIZATIONOUTPUT = 10000;   // enough for one fee worth to finalization and notarization thread
static const int32_t PBAAS_MINSTARTBLOCKDELTA = 50;         // minimum number of blocks to wait for starting a chain after definition
static const int32_t PBAAS_MAXPRIORBLOCKS = 16;             // maximum prior block commitments to include in prior blocks chain object

extern int64_t PBAAS_PRECONVERSION;
extern int64_t PBAAS_MINPRECONVERT;
extern int64_t PBAAS_MAXPRECONVERT;

enum CURRENCY_OPTIONS {
    CURRENCY_FUNGIBLE = 1,
    CURRENCY_FRACTIONAL = 2
};

// we wil uncomment service types as they are implemented
// commented service types are here as guidance and reminders
enum PBAAS_SERVICE_TYPES {
    SERVICE_INVALID = 0,
    SERVICE_NOTARIZATION = 1,
    //SERVICE_NODE = 2,
    //SERVICE_ELECTRUM = 3,
    SERVICE_LAST = 1
};

// these are object types that can be stored and recognized in an opret array
enum CHAIN_OBJECT_TYPES
{
    CHAINOBJ_INVALID = 0,
    CHAINOBJ_HEADER = 1,            // serialized full block header
    CHAINOBJ_TRANSACTION = 2,       // serialized transaction, sometimes without an opret, which will be reconstructed
    CHAINOBJ_PROOF = 3,             // merkle proof of preceding block or transaction
    CHAINOBJ_HEADER_REF = 4,        // equivalent to header, but only includes non-canonical data, assuming merge mine reconstruction
    CHAINOBJ_PRIORBLOCKS = 5,       // prior block commitments to ensure recognition of overlapping notarizations
    CHAINOBJ_RESERVETRANSFER = 6,   // serialized transaction, sometimes without an opret, which will be reconstructed
    CHAINOBJ_CROSSCHAINPROOF = 7    // proof that references a notarization
};

// the proof of an opret transaction, which is simply the types of objects and hashes of each
class COpRetProof
{
public:
    uint32_t orIndex;                   // index into the opret objects to begin with
    std::vector<uint8_t>    types;
    std::vector<uint256>    hashes;

    COpRetProof() : orIndex(0), types(0), hashes(0) {}
    COpRetProof(std::vector<uint8_t> &rTypes, std::vector<uint256> &rHashes, uint32_t opretIndex = 0) : types(rTypes), hashes(rHashes), orIndex(opretIndex) {}

    void AddObject(CHAIN_OBJECT_TYPES typeCode, uint256 objHash)
    {
        types.push_back(typeCode);
        hashes.push_back(objHash);
    }

    template <typename CHAINOBJTYPE>
    void AddObject(CHAINOBJTYPE &co, uint256 objHash)
    {
        types.push_back(ObjTypeCode(co));
        hashes.push_back(objHash);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(orIndex);
        READWRITE(types);
        READWRITE(hashes);
    }
};

class CHeaderRef
{
public:
    uint256 hash;               // block hash
    CPBaaSPreHeader preHeader;  // non-canonical pre-header data of source chain

    CHeaderRef() : hash() {}
    CHeaderRef(uint256 &rHash, CPBaaSPreHeader ph) : hash(rHash), preHeader(ph) {}
    CHeaderRef(const CBlockHeader &bh) : hash(bh.GetHash()), preHeader(bh) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hash);
        READWRITE(preHeader);
    }

    uint256 GetHash() { return hash; }
};

class CPriorBlocksCommitment
{
public:
    std::vector<uint256> priorBlocks;       // prior block commitments, which are node hashes that include merkle root, block hash, and compact power

    CPriorBlocksCommitment() : priorBlocks() {}
    CPriorBlocksCommitment(std::vector<uint256> priors) : priorBlocks(priors) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(priorBlocks);
    }
};

class CCrossChainProof
{
public:
    uint256 notarizationRoot;               // notarization txid used as the root of proof
    CMerkleBranch branch;                   // proof of the transaction on the other chain and import, transaction is stored separately

    CCrossChainProof() {}
    CCrossChainProof(const uint256 &rootTxId, const CMerkleBranch &b) : notarizationRoot(rootTxId), branch(b) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(notarizationRoot);
        READWRITE(branch);
    }

    bool IsValid() const
    {
        return !notarizationRoot.IsNull();
    }
};

class CBaseChainObject
{
public:
    uint16_t objectType;                    // type of object, such as blockheader, transaction, proof, tokentx, etc.

    CBaseChainObject() : objectType(CHAINOBJ_INVALID) {}
    CBaseChainObject(uint16_t objType) : objectType(objType) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(objectType);
    }
};

template <typename SERIALIZABLE>
class CChainObject : public CBaseChainObject
{
public:
    SERIALIZABLE object;                    // the actual object

    CChainObject() : CBaseChainObject() {}

    CChainObject(uint16_t objType, const SERIALIZABLE &rObject) : CBaseChainObject(objType)
    {
        object = rObject;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(objectType);
        READWRITE(object);
    }

    uint256 GetHash() const
    {
        CHashWriter hw(SER_GETHASH, PROTOCOL_VERSION);

        hw << object;
        return GetHash();
    }
};

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
uint256 GetChainObjectHash(const CBaseChainObject &bo);

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
template <typename OStream>
CBaseChainObject *RehydrateChainObject(OStream &s)
{
    uint16_t objType;

    s >> objType;

    union {
        CChainObject<CBlockHeader> *pNewHeader;
        CChainObject<CTransaction> *pNewTx;
        CChainObject<CMerkleBranch> *pNewProof;
        CChainObject<CHeaderRef> *pNewHeaderRef;
        CChainObject<CPriorBlocksCommitment> *pPriors;
        CChainObject<CReserveTransfer> *pExport;
        CChainObject<CCrossChainProof> *pCrossChainProof;
        CBaseChainObject *retPtr;
    };

    retPtr = NULL;

    switch(objType)
    {
        case CHAINOBJ_HEADER:
            pNewHeader = new CChainObject<CBlockHeader>();
            if (pNewHeader)
            {
                s >> pNewHeader->object;
                pNewHeader->objectType = objType;
            }
            break;
        case CHAINOBJ_TRANSACTION:
            pNewTx = new CChainObject<CTransaction>();
            if (pNewTx)
            {
                s >> pNewTx->object;
                pNewTx->objectType = objType;
            }
            break;
        case CHAINOBJ_PROOF:
            pNewProof = new CChainObject<CMerkleBranch>();
            if (pNewProof)
            {
                s >> pNewProof->object;
                pNewProof->objectType = objType;
            }
            break;
        case CHAINOBJ_HEADER_REF:
            pNewHeaderRef = new CChainObject<CHeaderRef>();
            if (pNewHeaderRef)
            {
                s >> pNewHeaderRef->object;
                pNewHeaderRef->objectType = objType;
            }
            break;
        case CHAINOBJ_PRIORBLOCKS:
            pPriors = new CChainObject<CPriorBlocksCommitment>();
            if (pPriors)
            {
                s >> pPriors->object;
                pPriors->objectType = objType;
            }
            break;
        case CHAINOBJ_RESERVETRANSFER:
            pExport = new CChainObject<CReserveTransfer>();
            if (pExport)
            {
                s >> pExport->object;
                pExport->objectType = objType;
            }
            break;
        case CHAINOBJ_CROSSCHAINPROOF:
            pCrossChainProof = new CChainObject<CCrossChainProof>();
            if (pCrossChainProof)
            {
                s >> pCrossChainProof->object;
                pCrossChainProof->objectType = objType;
            }
            break;
    }
    return retPtr;
}

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
template <typename OStream>
bool DehydrateChainObject(OStream &s, const CBaseChainObject *pobj)
{
    switch(pobj->objectType)
    {
        case CHAINOBJ_HEADER:
        {
            s << *(CChainObject<CBlockHeader> *)pobj;
            return true;
        }

        case CHAINOBJ_TRANSACTION:
        {
            s << *(CChainObject<CTransaction> *)pobj;
            return true;
        }

        case CHAINOBJ_PROOF:
        {
            s << *(CChainObject<CMerkleBranch> *)pobj;
            return true;
        }

        case CHAINOBJ_HEADER_REF:
        {
            s << *(CChainObject<CHeaderRef> *)pobj;
            return true;
        }

        case CHAINOBJ_PRIORBLOCKS:
        {
            s << *(CChainObject<CPriorBlocksCommitment> *)pobj;
            return true;
        }

        case CHAINOBJ_RESERVETRANSFER:
        {
            s << *(CChainObject<CReserveTransfer> *)pobj;
            return true;
        }
        case CHAINOBJ_CROSSCHAINPROOF:
        {
            s << *(CChainObject<CCrossChainProof> *)pobj;
            return true;
        }
    }
    return false;
}

int8_t ObjTypeCode(const CBlockHeader &obj);

int8_t ObjTypeCode(const CMerkleBranch &obj);

int8_t ObjTypeCode(const CTransaction &obj);

int8_t ObjTypeCode(const CHeaderRef &obj);

int8_t ObjTypeCode(const CPriorBlocksCommitment &obj);

int8_t ObjTypeCode(const CReserveTransfer &obj);

// this adds an opret to a mutable transaction that provides the necessary evidence of a signed, cheating stake transaction
CScript StoreOpRetArray(std::vector<CBaseChainObject *> &objPtrs);

void DeleteOpRetObjects(std::vector<CBaseChainObject *> &ora);

std::vector<CBaseChainObject *> RetrieveOpRetArray(const CScript &opRetScript);

// This data structure is used on an output that provides proof of stake validation for other crypto conditions
// with rate limited spends based on a PoS contest
class CPoSSelector
{
public:
    uint32_t nBits;                         // PoS difficulty target
    uint32_t nTargetSpacing;                // number of 1/1000ths of a block between selections (e.g. 1 == 1000 selections per block)

    CPoSSelector(uint32_t bits, uint32_t TargetSpacing)
    {
        nBits = bits; 
        nTargetSpacing = TargetSpacing;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nBits);
        READWRITE(nTargetSpacing);
    }

    CPoSSelector(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        return nBits != 0;
    }
};

// Additional data for an output pool used for a PBaaS chain's reward for service, such as mining, staking, node or electrum service
class CServiceReward
{
public:
    uint32_t nVersion;                      // version of this chain definition data structure to allow for extensions (not daemon version)
    uint16_t serviceType;                   // type of service
    int32_t billingPeriod;                  // this is used to identify to which billing period of a chain, this reward applies

    CServiceReward() : nVersion(PBAAS_VERSION_INVALID), serviceType(SERVICE_INVALID) {}

    CServiceReward(PBAAS_SERVICE_TYPES ServiceType, int32_t period) : nVersion(PBAAS_VERSION), serviceType(ServiceType), billingPeriod(period) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(serviceType);
        READWRITE(billingPeriod);
    }

    CServiceReward(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CServiceReward(const UniValue &obj) : nVersion(PBAAS_VERSION)
    {
        serviceType = uni_get_str(find_value(obj, "servicetype")) == "notarization" ? SERVICE_NOTARIZATION : SERVICE_INVALID;
        billingPeriod = uni_get_int(find_value(obj, "billingperiod"));
        if (!billingPeriod)
        {
            serviceType = SERVICE_INVALID;
        }
    }

    CServiceReward(const CTransaction &tx, bool validate = false);

    UniValue ToUniValue() const
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("servicetype", serviceType == SERVICE_NOTARIZATION ? "notarization" : "unknown"));
        obj.push_back(Pair("billingperiod", billingPeriod));
        return obj;
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        return serviceType != SERVICE_INVALID;
    }
};

class CInputDescriptor
{
public:
    CScript scriptPubKey;
    CAmount nValue;
    CTxIn txIn;
    CInputDescriptor() : nValue(0) {}
    CInputDescriptor(CScript script, CAmount value, CTxIn input) : scriptPubKey(script), nValue(value), txIn(input) {}
};

class CRPCChainData
{
public:
    CPBaaSChainDefinition chainDefinition;  // chain information for the specific chain
    std::string     rpcHost;                // host of the chain's daemon
    int32_t         rpcPort;                // port of the chain's daemon
    std::string     rpcUserPass;            // user and password for this daemon

    CRPCChainData() {}
    CRPCChainData(CPBaaSChainDefinition &chainDef, std::string host, int32_t port, std::string userPass) :
        chainDefinition(chainDef), rpcHost{host}, rpcPort(port), rpcUserPass(userPass) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainDefinition);
        READWRITE(rpcHost);
        READWRITE(rpcPort);
        READWRITE(rpcUserPass);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        return chainDefinition.IsValid();
    }

    uint160 GetChainID()
    {
        return chainDefinition.GetChainID();
    }
};

// Each merge mined chain gets an entry that includes information required to connect to a live daemon
// for that block, cross notarize, and validate notarizations.
class CPBaaSMergeMinedChainData : public CRPCChainData
{
public:
    static const uint32_t MAX_MERGE_CHAINS = 15;
    CBlock          block;                  // full block to submit upon winning header

    CPBaaSMergeMinedChainData() {}
    CPBaaSMergeMinedChainData(CPBaaSChainDefinition &chainDef, std::string host, int32_t port, std::string userPass, CBlock &blk) :
        CRPCChainData(chainDef, host, port, userPass), block(blk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainDefinition);
        READWRITE(rpcHost);
        READWRITE(rpcPort);
        READWRITE(rpcUserPass);
        READWRITE(block);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }
};

class CConnectedChains
{
protected:
    CPBaaSMergeMinedChainData *GetChainInfo(uint160 chainID);

public:
    std::map<uint160, CPBaaSMergeMinedChainData> mergeMinedChains;
    std::map<arith_uint256, CPBaaSMergeMinedChainData *> mergeMinedTargets;

    std::string notaryChainVersion;             // is Verus or VRSCTEST running and PBaaS compatible?
    int32_t notaryChainHeight;                  // is Verus or VRSCTEST running and PBaaS compatible?
    CRPCChainData notaryChain;                  // notary chain information

    CPBaaSChainDefinition thisChain;
    std::vector<std::pair<int, CScript>> latestMiningOutputs; // accessible from all merge miners - can be invalid
    CTxDestination  latestDestination;          // latest destination from miner output 0 - can be invalid
    int64_t lastAggregation = 0;                // adjusted time of last aggregation

    int32_t earnedNotarizationHeight;           // zero or the height of one or more potential submissions
    CBlock earnedNotarizationBlock;
    int32_t earnedNotarizationIndex;            // index of earned notarization in block

    bool dirty;
    bool lastSubmissionFailed;                  // if we submit a failed block, make another
    std::map<arith_uint256, CBlockHeader> qualifiedHeaders;

    CCriticalSection cs_mergemining;
    CSemaphore sem_submitthread;

    CConnectedChains() : sem_submitthread(0), earnedNotarizationHeight(0), dirty(0), lastSubmissionFailed(0) {}

    arith_uint256 LowestTarget()
    {
        if (mergeMinedTargets.size())
        {
            return mergeMinedTargets.begin()->first;
        }
        else
        {
            return arith_uint256(0);
        }
    }

    void SubmissionThread();
    static void SubmissionThreadStub();
    std::vector<std::pair<std::string, UniValue>> SubmitQualifiedBlocks();

    void QueueNewBlockHeader(CBlockHeader &bh);
    void QueueEarnedNotarization(CBlock &blk, int32_t txIndex, int32_t height);

    bool AddMergedBlock(CPBaaSMergeMinedChainData &blkData);
    bool RemoveMergedBlock(uint160 chainID);
    bool GetChainInfo(uint160 chainID, CRPCChainData &rpcChainData);
    void PruneOldChains(uint32_t pruneBefore);
    uint32_t CombineBlocks(CBlockHeader &bh);

    // returns false if destinations are empty or first is not either pubkey or pubkeyhash
    bool SetLatestMiningOutputs(const std::vector<std::pair<int, CScript>> &minerOutputs, CTxDestination &firstDestinationOut);
    void AggregateChainTransfers(const CTxDestination &feeOutput, uint32_t nHeight);

    // send new imports from this chain to the specified chain, which generally will be the notary chain
    void SendNewImports(const uint160 &chainID, 
                        const CPBaaSNotarization &notarization, 
                        const uint256 &lastExportTx, 
                        const CTransaction &lastCrossImport, 
                        const CTransaction &lastExport);

    bool GetLastImport(const uint160 &chainID, 
                       CTransaction &lastImport, 
                       CTransaction &crossChainExport, 
                       CCrossChainImport &ccImport, 
                       CCrossChainExport &ccCrossExport);

    // returns newly created import transactions to the specified chain from exports on this chain specified chain
    bool CreateLatestImports(const CPBaaSChainDefinition &chainDef, 
                             const CTransaction &lastCrossChainImport, 
                             const CTransaction &importTxTemplate,
                             const CTransaction &lastConfirmedNotarization,
                             CAmount totalAvailableInput,
                             std::vector<CTransaction> &newImports);

    CRPCChainData &NotaryChain()
    {
        return notaryChain;
    }

    CPBaaSChainDefinition &ThisChain()
    {
        return thisChain;
    }

    CCoinbaseCurrencyState GetCurrencyState(int32_t height);

    bool CheckVerusPBaaSAvailable(UniValue &chainInfo, UniValue &chainDef);
    bool CheckVerusPBaaSAvailable();      // may use RPC to call Verus
    bool IsVerusPBaaSAvailable();
    std::vector<CPBaaSChainDefinition> GetMergeMinedChains()
    {
        std::vector<CPBaaSChainDefinition> ret;
        LOCK(cs_mergemining);
        for (auto &chain : mergeMinedChains)
        {
            ret.push_back(chain.second.chainDefinition);
        }
        return ret;
    }
};

template <typename TOBJ>
CTxOut MakeCC1of1Vout(uint8_t evalcode, CAmount nValue, CPubKey pk, std::vector<CTxDestination> vDest, const TOBJ &obj)
{
    assert(vDest.size() < 256);

    CTxOut vout;
    CC *payoutCond = MakeCCcond1(evalcode, pk);
    vout = CTxOut(nValue, CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<std::vector<unsigned char>> vvch({::AsVector((const TOBJ)obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 1, (uint8_t)(vDest.size()), vDest, vvch);

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

template <typename TOBJ>
CTxOut MakeCC1ofAnyVout(uint8_t evalcode, CAmount nValue, std::vector<CTxDestination> vDest, const TOBJ &obj, const CPubKey &pk=CPubKey())
{
    // if pk is valid, we will make sure that it is one of the signature options on this CC
    if (pk.IsValid())
    {
        CCcontract_info C;
        CCcontract_info *cp;
        cp = CCinit(&C, evalcode);
        int i;
        bool addPubKey = false;
        for (i = 0; i < vDest.size(); i++)
        {
            CPubKey oneKey(boost::apply_visitor<GetPubKeyForPubKey>(GetPubKeyForPubKey(), vDest[i]));
            if ((oneKey.IsValid() && oneKey == pk) || CKeyID(GetDestinationID(vDest[i])) == pk.GetID())
            {
                // found, so don't add
                break;
            }
        }
        // if not found, add the pubkey
        if (i >= vDest.size())
        {
            vDest.push_back(CTxDestination(pk));
        }
    }

    CTxOut vout;
    CC *payoutCond = MakeCCcondAny(evalcode, vDest);
    vout = CTxOut(nValue, CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<std::vector<unsigned char>> vvch({::AsVector((const TOBJ)obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 0, (uint8_t)(vDest.size()), vDest, vvch);

    for (auto dest : vDest)
    {
        CPubKey oneKey(boost::apply_visitor<GetPubKeyForPubKey>(GetPubKeyForPubKey(), dest));
        std::vector<unsigned char> bytes = GetDestinationBytes(dest);
        if ((!oneKey.IsValid() && bytes.size() != 20) || (bytes.size() != 33 && bytes.size() != 20))
        {
            printf("Invalid destination %s\n", EncodeDestination(dest).c_str());
        }
    }

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

template <typename TOBJ>
CTxOut MakeCC1of2Vout(uint8_t evalcode, CAmount nValue, CPubKey pk1, CPubKey pk2, const TOBJ &obj)
{
    CTxOut vout;
    CC *payoutCond = MakeCCcond1of2(evalcode, pk1, pk2);
    vout = CTxOut(nValue,CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<CPubKey> vpk({pk1, pk2});
    std::vector<std::vector<unsigned char>> vvch({::AsVector((const TOBJ)obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 1, 2, vpk, vvch);

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

template <typename TOBJ>
CTxOut MakeCC1of2Vout(uint8_t evalcode, CAmount nValue, CPubKey pk1, CPubKey pk2, std::vector<CTxDestination> vDest, const TOBJ &obj)
{
    CTxOut vout;
    CC *payoutCond = MakeCCcond1of2(evalcode, pk1, pk2);
    vout = CTxOut(nValue,CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<CPubKey> vpk({pk1, pk2});
    std::vector<std::vector<unsigned char>> vvch({::AsVector((const TOBJ)obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 1, (uint8_t)(vDest.size()), vDest, vvch);

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

bool IsVerusActive();

// used to export coins from one chain to another, if they are not native, they are represented on the other
// chain as tokens
bool ValidateCrossChainExport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsCrossChainExportInput(const CScript &scriptSig);

// used to validate import of coins from one chain to another. if they are not native and are supported,
// they are represented o the chain as tokens
bool ValidateCrossChainImport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsCrossChainImportInput(const CScript &scriptSig);

// used to validate a specific service reward based on the spending transaction
bool ValidateServiceReward(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsServiceRewardInput(const CScript &scriptSig);

// used as a proxy token output for a reserve currency on its fractional reserve chain
bool ValidateReserveOutput(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsReserveOutputInput(const CScript &scriptSig);

// used to transfer a reserve currency between chains
bool ValidateReserveTransfer(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsReserveTransferInput(const CScript &scriptSig);

// used as exchange tokens between reserves and fractional reserves
bool ValidateReserveExchange(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsReserveExchangeInput(const CScript &scriptSig);

// used to deposit reserves into a reserve UTXO set
bool ValidateReserveDeposit(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsReserveDepositInput(const CScript &scriptSig);

bool ValidateChainDefinition(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsChainDefinitionInput(const CScript &scriptSig);

bool ValidateCurrencyState(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled);
bool IsCurrencyStateInput(const CScript &scriptSig);

bool GetCCParams(const CScript &scr, COptCCParams &ccParams);

bool SetThisChain(UniValue &chainDefinition);

extern CConnectedChains ConnectedChains;
extern uint160 ASSETCHAINS_CHAINID;

#endif
