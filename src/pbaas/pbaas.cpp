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
 */

#include "pbaas/crosschainrpc.h"
#include "pbaas/pbaas.h"
#include "pbaas/notarization.h"
#include "pbaas/identity.h"
#include "rpc/pbaasrpc.h"
#include "base58.h"
#include "timedata.h"
#include "main.h"
#include "transaction_builder.h"

using namespace std;

CConnectedChains ConnectedChains;

bool IsVerusActive()
{
    return (strcmp(ASSETCHAINS_SYMBOL, "VRSC") == 0 || strcmp(ASSETCHAINS_SYMBOL, "VRSCTEST") == 0);
}

// this adds an opret to a mutable transaction and returns the voutnum if it could be added
int32_t AddOpRetOutput(CMutableTransaction &mtx, const CScript &opRetScript)
{
    if (opRetScript.IsOpReturn() && opRetScript.size() <= MAX_OP_RETURN_RELAY)
    {
        CTxOut vOut = CTxOut();
        vOut.scriptPubKey = opRetScript;
        vOut.nValue = 0;
        mtx.vout.push_back(vOut);
        return mtx.vout.size() - 1;
    }
    else
    {
        return -1;
    }
}

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
uint256 GetChainObjectHash(const CBaseChainObject &bo)
{
    union {
        const CChainObject<CBlockHeader> *pNewHeader;
        const CChainObject<CTransaction> *pNewTx;
        const CChainObject<CMerkleBranch> *pNewProof;
        const CChainObject<CHeaderRef> *pNewHeaderRef;
        const CChainObject<CPriorBlocksCommitment> *pPriors;
        const CChainObject<CReserveTransfer> *pExport;
        const CChainObject<CCrossChainProof> *pCrossChainProof;
        const CBaseChainObject *retPtr;
    };

    retPtr = &bo;

    switch(bo.objectType)
    {
        case CHAINOBJ_HEADER:
            return pNewHeader->GetHash();

        case CHAINOBJ_TRANSACTION:
            return pNewTx->GetHash();

        case CHAINOBJ_PROOF:
            return pNewProof->GetHash();

        case CHAINOBJ_HEADER_REF:
            return pNewHeaderRef->GetHash();

        case CHAINOBJ_PRIORBLOCKS:
            return pPriors->GetHash();

        case CHAINOBJ_RESERVETRANSFER:
            return pExport->GetHash();

        case CHAINOBJ_CROSSCHAINPROOF:
            return pCrossChainProof->GetHash();

    }
    return uint256();
}

// used to export coins from one chain to another, if they are not native, they are represented on the other
// chain as tokens
bool ValidateCrossChainExport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}

bool IsCrossChainExportInput(const CScript &scriptSig)
{
    return true;
}

// used to validate import of coins from one chain to another. if they are not native and are supported,
// they are represented o the chain as tokens
bool ValidateCrossChainImport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsCrossChainImportInput(const CScript &scriptSig)
{
    return true;
}

// used to validate a specific service reward based on the spending transaction
bool ValidateServiceReward(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    // for each type of service reward, we need to check and see if the spender is
    // correctly formatted to be a valid spend of the service reward. for notarization
    // we ensure that the notarization and its outputs are valid and that the spend
    // applies to the correct billing period
    return true;
}
bool IsServiceRewardInput(const CScript &scriptSig)
{
    return true;
}

// used as a proxy token output for a reserve currency on its fractional reserve chain
bool ValidateReserveOutput(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsReserveOutputInput(const CScript &scriptSig)
{
    return true;
}

bool ValidateReserveTransfer(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsReserveTransferInput(const CScript &scriptSig)
{
    return true;
}

bool ValidateReserveDeposit(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsReserveDepositInput(const CScript &scriptSig)
{
    return true;
}

bool ValidateCurrencyState(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsCurrencyStateInput(const CScript &scriptSig)
{
    return true;
}

// used to convert a fractional reserve currency into its reserve and back 
bool ValidateReserveExchange(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    return true;
}
bool IsReserveExchangeInput(const CScript &scriptSig)
{
    return true;
}


/*
 * Verifies that the input objects match the hashes and returns the transaction.
 * 
 * If the opRetTx has the op ret, this calculates based on the actual transaction and
 * validates the hashes. If the opRetTx does not have the opRet itself, this validates
 * by ensuring that all objects are present on this chain, composing the opRet, and
 * ensuring that the transaction then hashes to the correct txid.
 * 
 */
bool ValidateOpretProof(CScript &opRet, COpRetProof &orProof)
{
    // enumerate through the objects and validate that they are objects of the expected type that hash
    // to the value expected. return true if so
    return true;
}

int8_t ObjTypeCode(const CBlockHeader &obj)
{
    return CHAINOBJ_HEADER;
}

int8_t ObjTypeCode(const CMerkleBranch &obj)
{
    return CHAINOBJ_PROOF;
}

int8_t ObjTypeCode(const CTransaction &obj)
{
    return CHAINOBJ_TRANSACTION;
}

int8_t ObjTypeCode(const CHeaderRef &obj)
{
    return CHAINOBJ_HEADER_REF;
}

int8_t ObjTypeCode(const CPriorBlocksCommitment &obj)
{
    return CHAINOBJ_PRIORBLOCKS;
}

int8_t ObjTypeCode(const CReserveTransfer &obj)
{
    return CHAINOBJ_RESERVETRANSFER;
}

int8_t ObjTypeCode(const CCrossChainProof &obj)
{
    return CHAINOBJ_CROSSCHAINPROOF;
}

// this adds an opret to a mutable transaction that provides the necessary evidence of a signed, cheating stake transaction
CScript StoreOpRetArray(std::vector<CBaseChainObject *> &objPtrs)
{
    CScript vData;
    CDataStream s = CDataStream(SER_NETWORK, PROTOCOL_VERSION);
    s << (int32_t)OPRETTYPE_OBJECTARR;
    bool error = false;

    for (auto pobj : objPtrs)
    {
        try
        {
            if (!DehydrateChainObject(s, pobj))
            {
                error = true;
                break;
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            error = true;
            break;
        }
    }

    //std::vector<unsigned char> schars(s.begin(), s.begin() + 200);
    //printf("stream vector chars: %s\n", HexBytes(&schars[0], schars.size()).c_str());

    std::vector<unsigned char> vch(s.begin(), s.end());
    return error ? CScript() : CScript() << OP_RETURN << vch;
}

void DeleteOpRetObjects(std::vector<CBaseChainObject *> &ora)
{
    for (auto pobj : ora)
    {
        switch(pobj->objectType)
        {
            case CHAINOBJ_HEADER:
            {
                delete (CChainObject<CBlockHeader> *)pobj;
                break;
            }

            case CHAINOBJ_TRANSACTION:
            {
                delete (CChainObject<CTransaction> *)pobj;
                break;
            }

            case CHAINOBJ_PROOF:
            {
                delete (CChainObject<CMerkleBranch> *)pobj;
                break;
            }

            case CHAINOBJ_HEADER_REF:
            {
                delete (CChainObject<CHeaderRef> *)pobj;
                break;
            }

            case CHAINOBJ_PRIORBLOCKS:
            {
                delete (CChainObject<CPriorBlocksCommitment> *)pobj;
                break;
            }

            case CHAINOBJ_RESERVETRANSFER:
            {
                delete (CChainObject<CReserveTransfer> *)pobj;
                break;
            }

            case CHAINOBJ_CROSSCHAINPROOF:
            {
                delete (CChainObject<CCrossChainProof> *)pobj;
                break;
            }

            default:
                delete pobj;
        }
    }
    ora.clear();
}

std::vector<CBaseChainObject *> RetrieveOpRetArray(const CScript &opRetScript)
{
    std::vector<unsigned char> vch;
    std::vector<CBaseChainObject *> vRet;
    if (opRetScript.IsOpReturn() && GetOpReturnData(opRetScript, vch) && vch.size() > 0)
    {
        CDataStream s = CDataStream(vch, SER_NETWORK, PROTOCOL_VERSION);

        int32_t opRetType;

        try
        {
            s >> opRetType;
            if (opRetType == OPRETTYPE_OBJECTARR)
            {
                CBaseChainObject *pobj;
                while (!s.empty() && (pobj = RehydrateChainObject(s)))
                {
                    vRet.push_back(pobj);
                }
                if (!s.empty())
                {
                    printf("failed to load all objects in opret");
                    DeleteOpRetObjects(vRet);
                    vRet.clear();
                }
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            DeleteOpRetObjects(vRet);
            vRet.clear();
        }
    }
    return vRet;
}

CNodeData::CNodeData(UniValue &obj)
{
    networkAddress = uni_get_str(find_value(obj, "networkaddress"));
    CBitcoinAddress ba(uni_get_str(find_value(obj, "paymentaddress")));
    ba.GetKeyID(paymentAddress);
}

UniValue CNodeData::ToUniValue() const
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("networkaddress", networkAddress));
    obj.push_back(Pair("paymentaddress", CBitcoinAddress(paymentAddress).ToString()));
    return obj;
}

CPBaaSChainDefinition::CPBaaSChainDefinition(const UniValue &obj)
{
    nVersion = PBAAS_VERSION;
    name = std::string(uni_get_str(find_value(obj, "name")), 0, (KOMODO_ASSETCHAIN_MAXLEN - 1));

    string invalidChars = "\\/:*?\"<>|";
    for (int i = 0; i < name.size(); i++)
    {
        if (invalidChars.find(name[i]) != string::npos)
        {
            name[i] = '_';
        }
    }

    CBitcoinAddress ba(uni_get_str(find_value(obj, "paymentaddress")));
    ba.GetKeyID(address);
    premine = uni_get_int64(find_value(obj, "premine"));
    initialcontribution = uni_get_int64(find_value(obj, "initialcontribution"));
    conversion = uni_get_int64(find_value(obj, "conversion"));
    minpreconvert = uni_get_int64(find_value(obj, "minpreconvert"));
    maxpreconvert = uni_get_int64(find_value(obj, "maxpreconvert"));
    preconverted = uni_get_int64(find_value(obj, "preconverted"));
    launchFee = uni_get_int64(find_value(obj, "launchfee"));
    startBlock = uni_get_int(find_value(obj, "startblock"));
    endBlock = uni_get_int(find_value(obj, "endblock"));

    auto vEras = uni_getValues(find_value(obj, "eras"));
    if (vEras.size() > ASSETCHAINS_MAX_ERAS)
    {
        vEras.resize(ASSETCHAINS_MAX_ERAS);
    }

    for (auto era : vEras)
    {
        rewards.push_back(uni_get_int64(find_value(era, "reward")));
        rewardsDecay.push_back(uni_get_int64(find_value(era, "decay")));
        halving.push_back(uni_get_int64(find_value(era, "halving")));
        eraEnd.push_back(uni_get_int64(find_value(era, "eraend")));
        eraOptions.push_back(uni_get_int64(find_value(era, "eraoptions")));
    }

    billingPeriod = uni_get_int(find_value(obj, "billingperiod"));
    notarizationReward = uni_get_int64(find_value(obj, "notarizationreward"));

    auto nodeVec = uni_getValues(find_value(obj, "nodes"));
    for (auto node : nodeVec)
    {
        nodes.push_back(CNodeData(node));
    }
}

CPBaaSChainDefinition::CPBaaSChainDefinition(const CTransaction &tx, bool validate)
{
    bool definitionFound = false;
    nVersion = PBAAS_VERSION_INVALID;
    for (auto out : tx.vout)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(out.scriptPubKey, p))
        {
            if (p.evalCode == EVAL_PBAASDEFINITION)
            {
                if (definitionFound)
                {
                    nVersion = PBAAS_VERSION_INVALID;
                }
                else
                {
                    FromVector(p.vData[0], *this);
                    definitionFound = true;
                }
            }
        }
    }

    if (validate)
    {
        
    }
}

CServiceReward::CServiceReward(const CTransaction &tx, bool validate)
{
    nVersion = PBAAS_VERSION_INVALID;
    for (auto out : tx.vout)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(out.scriptPubKey, p))
        {
            // always take the first for now
            if (p.evalCode == EVAL_SERVICEREWARD)
            {
                FromVector(p.vData[0], *this);
                break;
            }
        }
    }

    if (validate)
    {
        
    }
}

CCrossChainExport::CCrossChainExport(const CTransaction &tx)
{
    for (auto out : tx.vout)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(out.scriptPubKey, p))
        {
            // always take the first for now
            if (p.evalCode == EVAL_CROSSCHAIN_EXPORT)
            {
                FromVector(p.vData[0], *this);
                break;
            }
        }
    }
}

uint160 CPBaaSChainDefinition::GetChainID(std::string name)
{
    uint160 parent;
    return CIdentity::GetID(name, parent);
}

uint160 CPBaaSChainDefinition::GetConditionID(int32_t condition) const
{
    return CCrossChainRPCData::GetConditionID(name, condition);
}

UniValue CPBaaSChainDefinition::ToUniValue() const
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("version", (int64_t)nVersion));
    obj.push_back(Pair("name", name));
    obj.push_back(Pair("chainid", GetChainID().GetHex()));
    obj.push_back(Pair("chainaddress", EncodeDestination(CTxDestination(CKeyID(GetChainID())))));
    obj.push_back(Pair("paymentaddress", EncodeDestination(CTxDestination(address))));
    obj.push_back(Pair("premine", (int64_t)premine));
    obj.push_back(Pair("initialcontribution", (int64_t)initialcontribution));
    obj.push_back(Pair("conversion", (int64_t)conversion));
    obj.push_back(Pair("minpreconvert", (int64_t)minpreconvert));
    obj.push_back(Pair("maxpreconvert", (int64_t)maxpreconvert));
    obj.push_back(Pair("preconverted", (int64_t)preconverted));
    obj.push_back(Pair("launchfee", (int64_t)launchFee));
    obj.push_back(Pair("conversionpercent", ValueFromAmount(conversion * 100)));
    obj.push_back(Pair("launchfeepercent", ValueFromAmount(launchFee * 100)));
    obj.push_back(Pair("startblock", (int32_t)startBlock));
    obj.push_back(Pair("endblock", (int32_t)endBlock));

    UniValue eraArr(UniValue::VARR);
    for (int i = 0; i < rewards.size(); i++)
    {
        UniValue era(UniValue::VOBJ);
        era.push_back(Pair("reward", rewards.size() > i ? rewards[i] : (int64_t)0));
        era.push_back(Pair("decay", rewardsDecay.size() > i ? rewardsDecay[i] : (int64_t)0));
        era.push_back(Pair("halving", halving.size() > i ? (int32_t)halving[i] : (int32_t)0));
        era.push_back(Pair("eraend", eraEnd.size() > i ? (int32_t)eraEnd[i] : (int32_t)0));
        era.push_back(Pair("eraoptions", eraOptions.size() > i ? (int32_t)eraOptions[i] : (int32_t)0));
        eraArr.push_back(era);
    }
    obj.push_back(Pair("eras", eraArr));

    obj.push_back(Pair("billingperiod", billingPeriod));
    obj.push_back(Pair("notarizationreward", notarizationReward));

    UniValue nodeArr(UniValue::VARR);
    for (auto node : nodes)
    {
        nodeArr.push_back(node.ToUniValue());
    }
    obj.push_back(Pair("nodes", nodeArr));

    return obj;
}

int CPBaaSChainDefinition::GetDefinedPort() const
{
    int port;
    string host;
    for (auto node : nodes)
    {
        SplitHostPort(node.networkAddress, port, host);
        if (port)
        {
            return port;
        }
    }
    return 0;
}

#define _ASSETCHAINS_TIMELOCKOFF 0xffffffffffffffff
extern uint64_t ASSETCHAINS_TIMELOCKGTE, ASSETCHAINS_TIMEUNLOCKFROM, ASSETCHAINS_TIMEUNLOCKTO;
extern int64_t ASSETCHAINS_SUPPLY, ASSETCHAINS_REWARD[3], ASSETCHAINS_DECAY[3], ASSETCHAINS_HALVING[3], ASSETCHAINS_ENDSUBSIDY[3], ASSETCHAINS_ERAOPTIONS[3];
extern int32_t PBAAS_STARTBLOCK, PBAAS_ENDBLOCK, ASSETCHAINS_LWMAPOS;
extern uint32_t ASSETCHAINS_ALGO, ASSETCHAINS_VERUSHASH, ASSETCHAINS_LASTERA;
extern std::string VERUS_CHAINNAME;

// adds the chain definition for this chain and nodes as well
// this also sets up the notarization chain, if there is one
bool SetThisChain(UniValue &chainDefinition)
{
    ConnectedChains.ThisChain() = CPBaaSChainDefinition(chainDefinition);

    if (!IsVerusActive())
    {
        CPBaaSChainDefinition &notaryChainDef = ConnectedChains.notaryChain.chainDefinition;
        // we set the notary chain to either Verus or VerusTest
        notaryChainDef.nVersion = PBAAS_VERSION;
        if (PBAAS_TESTMODE)
        {
            // setup Verus test parameters
            notaryChainDef.name = "VRSCTEST";
            notaryChainDef.premine = 5000000000000000;
            notaryChainDef.rewards = std::vector<int64_t>({2400000000});
            notaryChainDef.rewardsDecay = std::vector<int64_t>({0});
            notaryChainDef.halving = std::vector<int32_t>({225680});
            notaryChainDef.eraEnd = std::vector<int32_t>({0});
            notaryChainDef.eraOptions = std::vector<int32_t>({notaryChainDef.OPTION_ID_REFERRALS});
        }
        else
        {
            // first setup Verus parameters
            notaryChainDef.name = "VRSC";
            notaryChainDef.premine = 0;
            notaryChainDef.rewards = std::vector<int64_t>({0,38400000000,2400000000});
            notaryChainDef.rewardsDecay = std::vector<int64_t>({100000000,0,0});
            notaryChainDef.halving = std::vector<int32_t>({1,43200,1051920});
            notaryChainDef.eraEnd = std::vector<int32_t>({10080,226080,0});
            notaryChainDef.eraOptions = std::vector<int32_t>({notaryChainDef.OPTION_ID_REFERRALS,0,0});
        }
    }

    if (ConnectedChains.ThisChain().IsValid())
    {
        memset(ASSETCHAINS_SYMBOL, 0, sizeof(ASSETCHAINS_SYMBOL));
        strcpy(ASSETCHAINS_SYMBOL, ConnectedChains.ThisChain().name.c_str());

        // set all command line parameters into mapArgs from chain definition
        vector<string> nodeStrs;
        for (auto node : ConnectedChains.ThisChain().nodes)
        {
            nodeStrs.push_back(node.networkAddress);
        }
        if (nodeStrs.size())
        {
            mapMultiArgs["-seednode"] = nodeStrs;
        }
        if (int port = ConnectedChains.ThisChain().GetDefinedPort())
        {
            mapArgs["-port"] = to_string(port);
        }

        ASSETCHAINS_SUPPLY = ConnectedChains.ThisChain().premine;
        mapArgs["-ac_supply"] = to_string(ASSETCHAINS_SUPPLY);
        ASSETCHAINS_ALGO = ASSETCHAINS_VERUSHASH;
        ASSETCHAINS_LWMAPOS = 50;

        ASSETCHAINS_TIMELOCKGTE = _ASSETCHAINS_TIMELOCKOFF;
        ASSETCHAINS_TIMEUNLOCKFROM = 0;
        ASSETCHAINS_TIMEUNLOCKTO = 0;

        auto numEras = ConnectedChains.ThisChain().rewards.size();
        ASSETCHAINS_LASTERA = numEras - 1;
        mapArgs["-ac_eras"] = to_string(numEras);

        mapArgs["-ac_end"] = "";
        mapArgs["-ac_reward"] = "";
        mapArgs["-ac_halving"] = "";
        mapArgs["-ac_decay"] = "";
        mapArgs["-ac_options"] = "";

        for (int j = 0; j < ASSETCHAINS_MAX_ERAS; j++)
        {
            if (j > ASSETCHAINS_LASTERA)
            {
                ASSETCHAINS_REWARD[j] = ASSETCHAINS_REWARD[j-1];
                ASSETCHAINS_DECAY[j] = ASSETCHAINS_DECAY[j-1];
                ASSETCHAINS_HALVING[j] = ASSETCHAINS_HALVING[j-1];
                ASSETCHAINS_ENDSUBSIDY[j] = 0;
                ASSETCHAINS_ERAOPTIONS[j] = 0;
            }
            else
            {
                ASSETCHAINS_REWARD[j] = ConnectedChains.ThisChain().rewards[j];
                ASSETCHAINS_DECAY[j] = ConnectedChains.ThisChain().rewardsDecay[j];
                ASSETCHAINS_HALVING[j] = ConnectedChains.ThisChain().halving[j];
                ASSETCHAINS_ENDSUBSIDY[j] = ConnectedChains.ThisChain().eraEnd[j];
                ASSETCHAINS_ERAOPTIONS[j] = ConnectedChains.ThisChain().eraOptions[j];
                if (j == 0)
                {
                    mapArgs["-ac_reward"] = to_string(ASSETCHAINS_REWARD[j]);
                    mapArgs["-ac_decay"] = to_string(ASSETCHAINS_DECAY[j]);
                    mapArgs["-ac_halving"] = to_string(ASSETCHAINS_HALVING[j]);
                    mapArgs["-ac_end"] = to_string(ASSETCHAINS_ENDSUBSIDY[j]);
                    mapArgs["-ac_options"] = to_string(ASSETCHAINS_ERAOPTIONS[j]);
                }
                else
                {
                    mapArgs["-ac_reward"] += "," + to_string(ASSETCHAINS_REWARD[j]);
                    mapArgs["-ac_decay"] += "," + to_string(ASSETCHAINS_DECAY[j]);
                    mapArgs["-ac_halving"] += "," + to_string(ASSETCHAINS_HALVING[j]);
                    mapArgs["-ac_end"] += "," + to_string(ASSETCHAINS_ENDSUBSIDY[j]);
                    mapArgs["-ac_options"] += "," + to_string(ASSETCHAINS_ERAOPTIONS[j]);
                }
            }
        }

        PBAAS_STARTBLOCK = ConnectedChains.ThisChain().startBlock;
        mapArgs["-startblock"] = to_string(PBAAS_STARTBLOCK);
        PBAAS_ENDBLOCK = ConnectedChains.ThisChain().endBlock;
        mapArgs["-endblock"] = to_string(PBAAS_ENDBLOCK);

        PBAAS_PRECONVERSION = ConnectedChains.ThisChain().conversion;
        PBAAS_MINPRECONVERT = ConnectedChains.ThisChain().minpreconvert;
        PBAAS_MAXPRECONVERT = ConnectedChains.ThisChain().maxpreconvert;
        mapArgs["-ac_conversion"] = to_string(PBAAS_PRECONVERSION);
        mapArgs["-ac_minpreconvert"] = to_string(PBAAS_MINPRECONVERT);
        mapArgs["-ac_maxpreconvert"] = to_string(PBAAS_MAXPRECONVERT);
        return true;
    }
    else
    {
        return false;
    }
}

// ensures that the chain definition is valid and that there are no other definitions of the same name
// that have been confirmed.
bool ValidateChainDefinition(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn, bool fulfilled)
{
    // the chain definition output can be spent when the chain is at the end of its life and only then
    // TODO
    return false;
}

// ensures that the chain definition is valid and that there are no other definitions of the same name
// that have been confirmed.
bool CheckChainDefinitionOutput(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    // checked before a chain definition output script is accepted as a valid transaction

    // basics - we need a chain definition transaction to kick off a PBaaS chain. it must have:
    // 1) valid chain definition output with parameters in proper ranges and no duplicate name
    // 2) notarization output with conformant values
    // 3) finalization output
    // 3) notarization funding
    //

    // get the source transaction
    uint256 blkHash;
    CTransaction thisTx;
    if (!GetTransaction(tx.vin[nIn].prevout.hash, thisTx, blkHash))
    {
        LogPrintf("failed to retrieve transaction %s\n", tx.vin[nIn].prevout.hash.GetHex().c_str());
        return false;
    }

    CPBaaSChainDefinition chainDef(thisTx, true);
    CPBaaSNotarization notarization(thisTx, true);
    CNotarizationFinalization finalization(thisTx, true);

    if (!chainDef.IsValid() || !notarization.IsValid() || finalization.IsValid())
    {
        LogPrintf("transaction specified, %s, must have valid chain definition, notarization, and finaization outputs\n", tx.vin[nIn].prevout.hash.GetHex().c_str());
        return false;
    }

    CPBaaSChainDefinition prior;
    // this ensures that there is no other definition of the same name already on the blockchain
    if (!GetChainDefinition(chainDef.name, prior))
    {
        LogPrintf("PBaaS chain with the name %s already exists\n", chainDef.name.c_str());
        return false;
    }

    return true;
}

CAmount CCrossChainExport::CalculateExportFee() const
{
    if (numInputs > MAX_EXPORT_INPUTS)
    {
        return 0;
    }
    static const arith_uint256 satoshis(100000000);

    int64_t ratio = 50000000 + ((25000000 / MAX_EXPORT_INPUTS) * (numInputs - 1));

    return (((arith_uint256(totalFees) * arith_uint256(ratio))) / satoshis).GetLow64();
}

bool CConnectedChains::RemoveMergedBlock(uint160 chainID)
{
    bool retval = false;
    LOCK(cs_mergemining);

    //printf("RemoveMergedBlock ID: %s\n", chainID.GetHex().c_str());

    auto chainIt = mergeMinedChains.find(chainID);
    if (chainIt != mergeMinedChains.end())
    {
        arith_uint256 target;
        target.SetCompact(chainIt->second.block.nBits);
        for (auto removeRange = mergeMinedTargets.equal_range(target); removeRange.first != removeRange.second; removeRange.first++)
        {
            // make sure we don't just match by target
            if (removeRange.first->second->GetChainID() == chainID)
            {
                mergeMinedTargets.erase(removeRange.first);
                break;
            }
        }
        mergeMinedChains.erase(chainID);
        dirty = retval = true;

        // if we get to 0, give the thread a kick to stop waiting for mining
        //if (!mergeMinedChains.size())
        //{
        //    sem_submitthread.post();
        //}
    }
    return retval;
}

// remove merge mined chains added and not updated since a specific time
void CConnectedChains::PruneOldChains(uint32_t pruneBefore)
{
    vector<uint160> toRemove;

    LOCK(cs_mergemining);
    for (auto blkData : mergeMinedChains)
    {
        if (blkData.second.block.nTime < pruneBefore)
        {
            toRemove.push_back(blkData.first);
        }
    }

    for (auto id : toRemove)
    {
        //printf("Pruning chainID: %s\n", id.GetHex().c_str());
        RemoveMergedBlock(id);
    }
}

// adds or updates merge mined blocks
// returns false if failed to add
bool CConnectedChains::AddMergedBlock(CPBaaSMergeMinedChainData &blkData)
{
    // determine if we should replace one or add to the merge mine vector
    {
        LOCK(cs_mergemining);

        arith_uint256 target;
        uint160 cID = blkData.GetChainID();
        auto it = mergeMinedChains.find(cID);
        if (it != mergeMinedChains.end())
        {
            RemoveMergedBlock(cID);             // remove it if already there
        }
        target.SetCompact(blkData.block.nBits);

        //printf("AddMergedBlock name: %s, ID: %s\n", blkData.chainDefinition.name.c_str(), cID.GetHex().c_str());

        mergeMinedTargets.insert(make_pair(target, &(mergeMinedChains.insert(make_pair(cID, blkData)).first->second)));
        dirty = true;
    }
    return true;
}

bool CConnectedChains::GetChainInfo(uint160 chainID, CRPCChainData &rpcChainData)
{
    {
        LOCK(cs_mergemining);
        auto chainIt = mergeMinedChains.find(chainID);
        if (chainIt != mergeMinedChains.end())
        {
            rpcChainData = (CRPCChainData)chainIt->second;
            return true;
        }
        return false;
    }
}

// this returns a pointer to the data without copy and assumes the lock is held
CPBaaSMergeMinedChainData *CConnectedChains::GetChainInfo(uint160 chainID)
{
    {
        auto chainIt = mergeMinedChains.find(chainID);
        if (chainIt != mergeMinedChains.end())
        {
            return &chainIt->second;
        }
        return NULL;
    }
}

void CConnectedChains::QueueNewBlockHeader(CBlockHeader &bh)
{
    //printf("QueueNewBlockHeader %s\n", bh.GetHash().GetHex().c_str());
    {
        LOCK(cs_mergemining);

        qualifiedHeaders[UintToArith256(bh.GetHash())] = bh;
    }
    sem_submitthread.post();
}

// get the latest block header and submit one block at a time, returning after there are no more
// matching blocks to be found
vector<pair<string, UniValue>> CConnectedChains::SubmitQualifiedBlocks()
{
    std::set<uint160> inHeader;
    bool submissionFound;
    CPBaaSMergeMinedChainData chainData;
    vector<pair<string, UniValue>>  results;

    CBlockHeader bh;
    arith_uint256 lastHash;
    CPBaaSBlockHeader pbh;

    do
    {
        submissionFound = false;
        {
            LOCK(cs_mergemining);
            // attempt to submit with the lowest hash answers first to increase the likelihood of submitting
            // common, merge mined headers for notarization, drop out on any submission
            for (auto headerIt = qualifiedHeaders.begin(); !submissionFound && headerIt != qualifiedHeaders.end(); headerIt = qualifiedHeaders.begin())
            {
                // add the PBaaS chain ids from this header to a set for search
                for (uint32_t i = 0; headerIt->second.GetPBaaSHeader(pbh, i); i++)
                {
                    inHeader.insert(pbh.chainID);
                }

                uint160 chainID;
                // now look through all targets that are equal to or above the hash of this header
                for (auto chainIt = mergeMinedTargets.lower_bound(headerIt->first); !submissionFound && chainIt != mergeMinedTargets.end(); chainIt++)
                {
                    chainID = chainIt->second->GetChainID();
                    if (inHeader.count(chainID))
                    {
                        // first, check that the winning header matches the block that is there
                        CPBaaSPreHeader preHeader(chainIt->second->block);
                        preHeader.SetBlockData(headerIt->second);

                        // check if the block header matches the block's specific data, only then can we create a submission from this block
                        if (headerIt->second.CheckNonCanonicalData(chainID))
                        {
                            // save block as is, remove the block from merged headers, replace header, and submit
                            chainData = *chainIt->second;

                            *(CBlockHeader *)&chainData.block = headerIt->second;

                            submissionFound = true;
                        }
                        //else // not an error condition. code is here for debugging
                        //{
                        //    printf("Mismatch in non-canonical data for chain %s\n", chainIt->second->chainDefinition.name.c_str());
                        //}
                    }
                    //else // not an error condition. code is here for debugging
                    //{
                    //    printf("Not found in header %s\n", chainIt->second->chainDefinition.name.c_str());
                    //}
                }

                // if this header matched no block, discard and move to the next, otherwise, we'll drop through
                if (submissionFound)
                {
                    // once it is going to be submitted, remove block from this chain until a new one is added again
                    RemoveMergedBlock(chainID);
                    break;
                }
                else
                {
                    qualifiedHeaders.erase(headerIt);
                }
            }
        }
        if (submissionFound)
        {
            // submit one block and loop again. this approach allows multiple threads
            // to collectively empty the submission queue, mitigating the impact of
            // any one stalled daemon
            UniValue submitParams(UniValue::VARR);
            submitParams.push_back(EncodeHexBlk(chainData.block));
            UniValue result, error;
            try
            {
                result = RPCCall("submitblock", submitParams, chainData.rpcUserPass, chainData.rpcPort, chainData.rpcHost);
                result = find_value(result, "result");
                error = find_value(result, "error");
            }
            catch (exception e)
            {
                result = UniValue(e.what());
            }
            results.push_back(make_pair(chainData.chainDefinition.name, result));
            if (result.isStr() || !error.isNull())
            {
                printf("Error submitting block to %s chain: %s\n", chainData.chainDefinition.name.c_str(), result.isStr() ? result.get_str().c_str() : error.get_str().c_str());
            }
            else
            {
                printf("Successfully submitted block to %s chain\n", chainData.chainDefinition.name.c_str());
            }
        }
    } while (submissionFound);
    return results;
}

// add all merge mined chain PBaaS headers into the blockheader and return the easiest nBits target in the header
uint32_t CConnectedChains::CombineBlocks(CBlockHeader &bh)
{
    vector<uint160> inHeader;
    vector<UniValue> toCombine;
    arith_uint256 blkHash = UintToArith256(bh.GetHash());
    arith_uint256 target(0);
    
    CPBaaSBlockHeader pbh;

    {
        LOCK(cs_mergemining);

        CPBaaSSolutionDescriptor descr = CVerusSolutionVector::solutionTools.GetDescriptor(bh.nSolution);

        for (uint32_t i = 0; i < descr.numPBaaSHeaders; i++)
        {
            if (bh.GetPBaaSHeader(pbh, i))
            {
                inHeader.push_back(pbh.chainID);
            }
        }

        // loop through the existing PBaaS chain ids in the header
        // remove any that are not either this Chain ID or in our local collection and then add all that are present
        for (uint32_t i = 0; i < inHeader.size(); i++)
        {
            auto it = mergeMinedChains.find(inHeader[i]);
            if (inHeader[i] != ASSETCHAINS_CHAINID && (it == mergeMinedChains.end()))
            {
                bh.DeletePBaaSHeader(i);
            }
        }

        for (auto chain : mergeMinedChains)
        {
            // get the native PBaaS header for each chain and put it into the
            // header we are given
            // it must have itself in as a PBaaS header
            uint160 cid = chain.second.GetChainID();
            if (chain.second.block.GetPBaaSHeader(pbh, cid) != -1)
            {
                if (!bh.AddUpdatePBaaSHeader(pbh))
                {
                    LogPrintf("Failure to add PBaaS block header for %s chain\n", chain.second.chainDefinition.name.c_str());
                    break;
                }
                else
                {
                    arith_uint256 t;
                    t.SetCompact(chain.second.block.nBits);
                    if (t > target)
                    {
                        target = t;
                    }
                }
            }
            else
            {
                LogPrintf("Merge mined block for %s does not contain PBaaS information\n", chain.second.chainDefinition.name.c_str());
            }
            
        }
        dirty = false;
    }

    return target.GetCompact();
}

bool CConnectedChains::IsVerusPBaaSAvailable()
{
    return notaryChainVersion >= "0.5.9";
}

extern string PBAAS_HOST, PBAAS_USERPASS;
extern int32_t PBAAS_PORT;
bool CConnectedChains::CheckVerusPBaaSAvailable(UniValue &chainInfoUni, UniValue &chainDefUni)
{
    if (chainInfoUni.isObject() && chainDefUni.isObject())
    {
        UniValue uniVer = find_value(chainInfoUni, "VRSCversion");
        if (uniVer.isStr())
        {
            LOCK(cs_mergemining);
            notaryChainVersion = uni_get_str(uniVer);
            notaryChainHeight = uni_get_int(find_value(chainInfoUni, "blocks"));
            CPBaaSChainDefinition chainDef(chainDefUni);
            notaryChain = CRPCChainData(chainDef, PBAAS_HOST, PBAAS_PORT, PBAAS_USERPASS);
        }
    }
    return IsVerusPBaaSAvailable();
}

bool CConnectedChains::CheckVerusPBaaSAvailable()
{
    if (IsVerusActive())
    {
        notaryChainVersion = "";
    }
    else
    {
        // if this is a PBaaS chain, poll for presence of Verus / root chain and current Verus block and version number
        // tolerate only 15 second timeout
        UniValue chainInfo, chainDef;
        try
        {
            UniValue params(UniValue::VARR);
            chainInfo = find_value(RPCCallRoot("getinfo", params), "result");
            if (!chainInfo.isNull())
            {
                params.push_back(VERUS_CHAINNAME);
                chainDef = find_value(RPCCallRoot("getchaindefinition", params), "result");

                if (!chainDef.isNull() && CheckVerusPBaaSAvailable(chainInfo, chainDef))
                {
                    return true;
                }
            }
        } catch (exception e)
        {
        }
    }
    notaryChainVersion = "";
    return false;
}

CCoinbaseCurrencyState CConnectedChains::GetCurrencyState(int32_t height)
{
    CCoinbaseCurrencyState currencyState;
    CBlock block;
    LOCK(cs_main);
    bool isVerusActive = IsVerusActive();
    if (!isVerusActive && 
        CConstVerusSolutionVector::activationHeight.ActiveVersion(height) >= CActivationHeight::ACTIVATE_PBAAS &&
        height != 0 && 
        height <= chainActive.Height() && 
        chainActive[height] && 
        ReadBlockFromDisk(block, chainActive[height], Params().GetConsensus()) &&
        (currencyState = CCoinbaseCurrencyState(block.vtx[0])).IsValid())
    {
        return currencyState;
    }
    else
    {
        bool isReserve = (thisChain.ChainOptions() & thisChain.OPTION_RESERVE);
        CAmount preconvertedNative = currencyState.ReserveToNative(thisChain.preconverted, thisChain.conversion);
        CCurrencyState cState(thisChain.conversion, preconvertedNative, preconvertedNative, 0, isReserve ? thisChain.preconverted : 0, isReserve ? CCurrencyState::VALID + CCurrencyState::ISRESERVE : CCurrencyState::VALID);
        CAmount fees = 0;
        if (!height && !isVerusActive)
        {
            fees = CReserveTransactionDescriptor::CalculateAdditionalConversionFee(thisChain.preconverted);
        }
        return CCoinbaseCurrencyState(cState, thisChain.preconverted, 0, CReserveOutput(CReserveOutput::VALID, 0), thisChain.conversion, fees, fees);
    }
}

bool CConnectedChains::SetLatestMiningOutputs(const std::vector<pair<int, CScript>> &minerOutputs, CTxDestination &firstDestinationOut)
{
    LOCK(cs_mergemining);

    if (!minerOutputs.size() || !ExtractDestination(minerOutputs[0].second, firstDestinationOut))
    {
        return false;
    }
    latestMiningOutputs = minerOutputs;
    latestDestination = firstDestinationOut;
    return true;
}

void CConnectedChains::AggregateChainTransfers(const CTxDestination &feeOutput, uint32_t nHeight)
{
    // all chains aggregate reserve transfer transactions, so aggregate and add all necessary export transactions to the mem pool
    {
        if (!nHeight)
        {
            return;
        }

        multimap<uint160, pair<CInputDescriptor, CReserveTransfer>> transferOutputs;

        LOCK(cs_main);

        // get all available transfer outputs to aggregate into export transactions
        if (GetUnspentChainTransfers(transferOutputs))
        {
            if (!transferOutputs.size())
            {
                return;
            }
            std::vector<pair<CInputDescriptor, CReserveTransfer>> txInputs;
            std::multimap<uint160, pair<int, CInputDescriptor>> exportOutputs;

            uint160 bookEnd;
            bookEnd.SetHex("ffffffffffffffffffffffffffffffffffffffff");
            uint160 lastChain = bookEnd;

            // add a bookend entry at the end of transfer outputs to ensure that we try to export all before it
            transferOutputs.insert(make_pair(bookEnd, make_pair(CInputDescriptor(), CReserveTransfer())));

            CPBaaSChainDefinition lastChainDef;

            // merge all of the common chainID outputs into common export transactions if either MIN_BLOCKS blocks have passed since the last
            // export of that type, or there are MIN_INPUTS or more outputs to aggregate
            for (auto it = transferOutputs.begin(); it != transferOutputs.end(); it++)
            {
                // get chain target and see if it is the same
                if (lastChain == bookEnd || it->first == lastChain)
                {
                    txInputs.push_back(it->second);
                }
                else
                {
                    // when we get here, we have a consecutive number of transfer outputs to consume in txInputs
                    // we need an unspent export output to export
                    if (GetUnspentChainExports(lastChain, exportOutputs) && exportOutputs.size())
                    {
                        auto lastExport = *exportOutputs.begin();

                        if (((nHeight - lastExport.second.first) >= CCrossChainExport::MIN_BLOCKS) || (txInputs.size() >= CCrossChainExport::MIN_INPUTS))
                        {
                            boost::optional<CTransaction> oneExport;

                            if (!GetChainDefinition(lastChain, lastChainDef))
                            {
                                printf("%s: chain definition for export chainID %s not found\n\n", __func__, lastChain.GetHex().c_str());
                                LogPrintf("%s: chain definition for export chainID %s not found\n\n", __func__, lastChain.GetHex().c_str());
                                continue;
                            }

                            // make one or more transactions that spends the last export and all possible cross chain transfers
                            while (txInputs.size())
                            {
                                TransactionBuilder tb(Params().GetConsensus(), nHeight);

                                int numInputs = (txInputs.size() < CCrossChainExport::MAX_EXPORT_INPUTS) ? txInputs.size() : CCrossChainExport::MAX_EXPORT_INPUTS;

                                int inputsLeft = txInputs.size() - numInputs;

                                if (inputsLeft > 0 && inputsLeft < CCrossChainExport::MIN_INPUTS)
                                {
                                    inputsLeft += CCrossChainExport::MIN_INPUTS - inputsLeft;
                                    numInputs -= CCrossChainExport::MIN_INPUTS - inputsLeft;
                                    assert(numInputs > 0);
                                }

                                // each time through, we make one export transaction with the remainder or a subset of the
                                // reserve transfer inputs. inputs can be:
                                // 1. transfers of reserve for fractional reserve chains
                                // 2. pre-conversions for pre-launch participation in the premine
                                // 3. reserve market conversions to send between Verus and a fractional reserve chain and always output the native coin
                                //
                                // If we are on the Verus chain, all inputs will include native coins. On a PBaaS chain, inputs can either be native
                                // or reserve token inputs.
                                //
                                // On the Verus chain, total native amount, minus the fee, must be sent to the reserve address of the specific chain
                                // as reserve deposit with native coin equivalent. Pre-conversions and conversions will be realized on the PBaaS chain
                                // as part of the import process
                                // 
                                // If we are on the PBaaS chain, conversions must happen before coins are sent this way back to the reserve chain. 
                                // Verus reserve outputs can be directly aggregated and transferred, with fees paid through conversion and the 
                                // remaining Verus reserve coin will be burned on the PBaaS chain as spending it is allowed, once notarized, on the
                                // Verus chain.
                                //
                                CAmount totalTxFees = 0;
                                CAmount totalAmount = 0;
                                CAmount exportOutVal = 0;
                                std::vector<CBaseChainObject *> chainObjects;

                                // first, we must add the export output from the current export thread to this chain
                                if (oneExport.has_value())
                                {
                                    // spend the last export transaction output
                                    CTransaction &tx = oneExport.get();
                                    COptCCParams p;
                                    int j;
                                    for (j = 0; j < tx.vout.size(); j++)
                                    {
                                        if (::IsPayToCryptoCondition(tx.vout[j].scriptPubKey, p) && p.evalCode == EVAL_CROSSCHAIN_EXPORT)
                                        {
                                            break;
                                        }
                                    }

                                    // had to be found and valid if we made the tx
                                    assert(j < tx.vout.size() && p.IsValid());

                                    tb.AddTransparentInput(COutPoint(tx.GetHash(), j), tx.vout[j].scriptPubKey, tx.vout[j].nValue);
                                    exportOutVal = tx.vout[j].nValue;
                                }
                                else
                                {
                                    // spend the recentExportIt output
                                    tb.AddTransparentInput(lastExport.second.second.txIn.prevout, lastExport.second.second.scriptPubKey, lastExport.second.second.nValue);
                                    exportOutVal = lastExport.second.second.nValue;
                                }

                                COptCCParams p;
                                int fails = 0;
                                for (int j = 0; j < numInputs; j++)
                                {
                                    tb.AddTransparentInput(txInputs[j].first.txIn.prevout, txInputs[j].first.scriptPubKey, txInputs[j].first.nValue, txInputs[j].first.txIn.nSequence);
                                    if (IsVerusActive() && txInputs[j].second.nValue + txInputs[j].second.nFees > txInputs[j].first.nValue)
                                    {
                                        // if this transfer is invalid and claims to carry more funds than it does, we consume it since it won't properly verify as a transfer, and
                                        // it is too expensive to let it force evaluation repeatedly. this condition should not get by normal checks, but just in case, don't let it slow transfers
                                        // we should formalize this into a chain contribution or amount adjustment.
                                        printf("%s: transaction %s claims more value than it contains\n", __func__, txInputs[j].first.txIn.prevout.hash.GetHex().c_str());
                                        LogPrintf("%s: transaction %s claims more value than it contains\n", __func__, txInputs[j].first.txIn.prevout.hash.GetHex().c_str());
                                        fails++;
                                    }
                                    else
                                    {
                                        totalTxFees += txInputs[j].second.nFees;
                                        totalAmount += txInputs[j].second.nValue;
                                        chainObjects.push_back(new CChainObject<CReserveTransfer>(ObjTypeCode(txInputs[j].second), txInputs[j].second));
                                    }
                                }
                                if (fails == numInputs)
                                {
                                    // erase the inputs we've attempted to spend
                                    txInputs.erase(txInputs.begin(), txInputs.begin() + numInputs);
                                    continue;
                                }

                                CCrossChainExport ccx(lastChain, numInputs, totalAmount, totalTxFees);
                                CAmount exportFees = ccx.CalculateExportFee();
                                CReserveTransfer feeOut(CReserveTransfer::VALID + CReserveTransfer::SEND_BACK + CReserveTransfer::FEE_OUTPUT, exportFees, 0, GetDestinationID(feeOutput));
                                chainObjects.push_back(new CChainObject<CReserveTransfer>(ObjTypeCode(feeOut), feeOut));

                                // do a preliminary check
                                CReserveTransactionDescriptor rtxd;
                                std::vector<CTxOut> dummy;

                                if (!rtxd.AddReserveTransferImportOutputs(lastChainDef, chainObjects, dummy))
                                {
                                    DeleteOpRetObjects(chainObjects);

                                    printf("%s: failed to create valid exports\n", __func__);
                                    LogPrintf("%s: failed to create valid exports\n", __func__);

                                    // debugging output
                                    printf("%s: failed to export outputs:\n", __func__);
                                    for (auto oneout : dummy)
                                    {
                                        UniValue uniOut;
                                        ScriptPubKeyToJSON(oneout.scriptPubKey, uniOut, false);
                                        printf("%s\n", uniOut.write(true, 2).c_str());
                                    }
                                }
                                else
                                {
                                    // debugging output
                                    printf("%s: exported outputs:\n", __func__);
                                    for (auto oneout : dummy)
                                    {
                                        UniValue uniOut;
                                        ScriptPubKeyToJSON(oneout.scriptPubKey, uniOut, false);
                                        printf("%s\n", uniOut.write(true, 2).c_str());
                                    }

                                    CScript opRet = StoreOpRetArray(chainObjects);
                                    DeleteOpRetObjects(chainObjects);

                                    CCcontract_info CC;
                                    CCcontract_info *cp;
                                    cp = CCinit(&CC, EVAL_CROSSCHAIN_EXPORT);

                                    CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

                                    // send native amount of zero to a cross chain export output of the specific chain
                                    std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(CCrossChainRPCData::GetConditionID(lastChain, EVAL_CROSSCHAIN_EXPORT))});

                                    CTxOut exportOut = MakeCC1of1Vout(EVAL_CROSSCHAIN_EXPORT, exportOutVal, pk, dests, ccx);

                                    tb.AddTransparentOutput(exportOut.scriptPubKey, exportOutVal);

                                    // if we are on Verus chain, send all native funds to reserve deposit CC, which is equivalent to the reserve account
                                    // if we are on a PBaaS chain, input is burned when sent back for release
                                    if (IsVerusActive())
                                    {
                                        cp = CCinit(&CC, EVAL_RESERVE_DEPOSIT);
                                        pk = CPubKey(ParseHex(CC.CChexstr));

                                        // send the entire amount to a reserve transfer output of the specific chain
                                        // we receive our fee on the other chain or when it comes back
                                        dests = std::vector<CTxDestination>({CKeyID(CCrossChainRPCData::GetConditionID(lastChain, EVAL_RESERVE_DEPOSIT))});

                                        CReserveOutput ro(CReserveOutput::VALID, totalAmount + totalTxFees);

                                        CTxOut outToReserve = MakeCC1of1Vout(EVAL_RESERVE_DEPOSIT, 
                                                                            ro.nValue,
                                                                            pk,
                                                                            dests,
                                                                            ro);

                                        tb.AddTransparentOutput(outToReserve.scriptPubKey, ro.nValue);
                                    }

                                    tb.AddOpRet(opRet);
                                    tb.SetFee(0);

                                    TransactionBuilderResult buildResult(tb.Build());

                                    if (!buildResult.IsError() && buildResult.IsTx())
                                    {
                                        // replace the last one only if we have a valid new one
                                        CTransaction tx = buildResult.GetTxOrThrow();

                                        LOCK2(cs_main, mempool.cs);
                                        static int lastHeight = 0;
                                        // remove conflicts, so that we get in
                                        std::list<CTransaction> removed;
                                        mempool.removeConflicts(tx, removed);

                                        // add to mem pool, prioritize according to the fee we will get, and relay
                                        printf("Created and signed export transaction %s\n", tx.GetHash().GetHex().c_str());
                                        LogPrintf("Created and signed export transaction %s\n", tx.GetHash().GetHex().c_str());
                                        if (myAddtomempool(tx))
                                        {
                                            uint256 hash = tx.GetHash();
                                            mempool.PrioritiseTransaction(hash, hash.GetHex(), (double)(exportFees << 1), exportFees);
                                            RelayTransaction(tx);
                                        }
                                    }
                                    else
                                    {
                                        // we can't do any more useful work for this chain if we failed here
                                        printf("Failed to create export transaction: %s\n", buildResult.GetError().c_str());
                                        LogPrintf("Failed to create export transaction: %s\n", buildResult.GetError().c_str());
                                        break;
                                    }
                                }

                                // erase the inputs we've attempted to spend
                                txInputs.erase(txInputs.begin(), txInputs.begin() + numInputs);
                            }
                        }
                    }
                }
                lastChain = it->first;
            }
        }
    }
}

// send new imports from this chain to the specified chain, which generally will be the notary chain
void CConnectedChains::SendNewImports(const uint160 &chainID, 
                                      const CPBaaSNotarization &notarization, 
                                      const uint256 &lastExportTx, 
                                      const CTransaction &lastCrossImport, 
                                      const CTransaction &lastExport)
{
    // currently only support sending imports to  
}

void CConnectedChains::SubmissionThread()
{
    try
    {
        arith_uint256 lastHash;
        int64_t lastImportTime = 0;
        uint32_t lastHeight = 0;
        
        // wait for something to check on, then submit blocks that should be submitted
        while (true)
        {
            boost::this_thread::interruption_point();

            if (IsVerusActive())
            {
                // blocks get discarded after no refresh for 5 minutes by default, probably should be more often
                //printf("SubmissionThread: pruning\n");
                PruneOldChains(GetAdjustedTime() - 300);
                bool submit = false;
                {
                    LOCK(cs_mergemining);
                    if (mergeMinedChains.size() == 0 && qualifiedHeaders.size() != 0)
                    {
                        qualifiedHeaders.clear();
                    }
                    submit = qualifiedHeaders.size() != 0 && mergeMinedChains.size() != 0;

                    //printf("SubmissionThread: qualifiedHeaders.size(): %lu, mergeMinedChains.size(): %lu\n", qualifiedHeaders.size(), mergeMinedChains.size());
                }
                if (submit)
                {
                    //printf("SubmissionThread: calling submit qualified blocks\n");
                    SubmitQualifiedBlocks();
                }
                else
                {
                    //printf("SubmissionThread: waiting on sem\n");
                    sem_submitthread.wait();
                }
            }
            else
            {
                // if this is a PBaaS chain, poll for presence of Verus / root chain and current Verus block and version number
                if (CheckVerusPBaaSAvailable())
                {
                    // check to see if we have recently earned a block with an earned notarization that qualifies for
                    // submitting an accepted notarization
                    if (earnedNotarizationHeight)
                    {
                        CBlock blk;
                        int32_t txIndex = -1, height;
                        {
                            LOCK(cs_mergemining);
                            if (earnedNotarizationHeight && earnedNotarizationHeight <= chainActive.Height() && earnedNotarizationBlock.GetHash() == chainActive[earnedNotarizationHeight]->GetBlockHash())
                            {
                                blk = earnedNotarizationBlock;
                                earnedNotarizationBlock = CBlock();
                                txIndex = earnedNotarizationIndex;
                                height = earnedNotarizationHeight;
                                earnedNotarizationHeight = 0;
                            }
                        }

                        if (txIndex != -1)
                        {
                            //printf("SubmissionThread: testing notarization\n");
                            CTransaction lastConfirmed;
                            uint256 txId = CreateAcceptedNotarization(blk, txIndex, height);

                            if (!txId.IsNull())
                            {
                                printf("Submitted notarization for acceptance: %s\n", txId.GetHex().c_str());
                                LogPrintf("Submitted notarization for acceptance: %s\n", txId.GetHex().c_str());
                            }
                        }
                    }

                    // every "n" seconds, look for imports to include in our blocks from the Verus chain
                    if ((GetAdjustedTime() - lastImportTime) >= 30 || lastHeight < (chainActive.LastTip() ? 0 : chainActive.LastTip()->GetHeight()))
                    {
                        lastImportTime = GetAdjustedTime();
                        lastHeight = (chainActive.LastTip() ? 0 : chainActive.LastTip()->GetHeight());

                        // see if our notary has a confirmed notarization for us
                        UniValue params(UniValue::VARR);
                        UniValue result;

                        params.push_back(thisChain.name);

                        try
                        {
                            result = find_value(RPCCallRoot("getlastimportin", params), "result");
                        } catch (exception e)
                        {
                            result = NullUniValue;
                        }

                        if (!result.isNull())
                        {
                            auto txUniStr = find_value(result, "lastimporttransaction");
                            auto txLastConfirmedStr = find_value(result, "lastconfirmednotarization");
                            auto txTemplateStr = find_value(result, "importtxtemplate");
                            CAmount totalImportAvailable = uni_get_int64(find_value(result, "totalimportavailable"));

                            CTransaction lastImportTx, lastConfirmedTx, templateTx;

                            if (txUniStr.isStr() && txTemplateStr.isStr() && 
                                DecodeHexTx(lastImportTx, txUniStr.get_str()) && 
                                DecodeHexTx(lastConfirmedTx, txLastConfirmedStr.get_str()) && 
                                DecodeHexTx(templateTx, txTemplateStr.get_str()))
                            {
                                std::vector<CTransaction> importTxes;
                                if (CreateLatestImports(notaryChain.chainDefinition, lastImportTx, templateTx, lastConfirmedTx, totalImportAvailable, importTxes))
                                {
                                    for (auto importTx : importTxes)
                                    {
                                        UniValue txResult;
                                        params.setArray();
                                        params.push_back(EncodeHexTx(importTx));

                                        try
                                        {
                                            txResult = find_value(RPCCallRoot("signrawtransaction", params), "result");
                                            if (txResult.isObject() && !(txResult = find_value(txResult, "hex")).isNull() && txResult.isStr() && txResult.get_str().size())
                                            {
                                                params.setArray();
                                                params.push_back(txResult);
                                                txResult = find_value(RPCCallRoot("sendrawtransaction", params), "result");
                                            }
                                            else
                                            {
                                                txResult = NullUniValue;
                                            }
                                            
                                        } catch (exception e)
                                        {
                                            txResult = NullUniValue;
                                        }
                                        uint256 testId;
                                        if (txResult.isStr())
                                        {
                                            testId.SetHex(txResult.get_str());
                                        }
                                        if (testId.IsNull())
                                        {
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                sleep(3);
            }
            boost::this_thread::interruption_point();
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("Verus merge mining thread terminated\n");
    }
}

void CConnectedChains::SubmissionThreadStub()
{
    ConnectedChains.SubmissionThread();
}

void CConnectedChains::QueueEarnedNotarization(CBlock &blk, int32_t txIndex, int32_t height)
{
    // called after winning a block that contains an earned notarization
    // the earned notarization and its height are queued for processing by the submission thread
    // when a new notarization is added, older notarizations are removed, but all notarizations in the current height are
    // kept
    LOCK(cs_mergemining);

    // we only care about the last
    earnedNotarizationHeight = height;
    earnedNotarizationBlock = blk;
    earnedNotarizationIndex = txIndex;
}

bool IsChainDefinitionInput(const CScript &scriptSig)
{
    uint32_t ecode;
    return scriptSig.IsPayToCryptoCondition(&ecode) && ecode == EVAL_PBAASDEFINITION;
}

