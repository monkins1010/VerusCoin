// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2019 Michael Toutonghi
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "core_io.h"
#ifdef ENABLE_MINING
#include "crypto/equihash.h"
#endif
#include "init.h"
#include "main.h"
#include "metrics.h"
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpc/server.h"
#include "txmempool.h"
#include "util.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include "timedata.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

#include "rpc/pbaasrpc.h"
#include "pbaas/crosschainrpc.h"
#include "pbaas/identity.h"
#include "transaction_builder.h"

using namespace std;

extern int32_t ASSETCHAINS_ALGO, ASSETCHAINS_EQUIHASH, ASSETCHAINS_LWMAPOS;
extern char ASSETCHAINS_SYMBOL[KOMODO_ASSETCHAIN_MAXLEN];
extern uint64_t ASSETCHAINS_STAKED;
extern int32_t KOMODO_MININGTHREADS;
extern bool VERUS_MINTBLOCKS;
extern uint8_t NOTARY_PUBKEY33[33];
extern uint160 ASSETCHAINS_CHAINID;
extern uint160 VERUS_CHAINID;
extern std::string VERUS_CHAINNAME;
extern int32_t USE_EXTERNAL_PUBKEY;
extern std::string NOTARY_PUBKEY;

arith_uint256 komodo_PoWtarget(int32_t *percPoSp,arith_uint256 target,int32_t height,int32_t goalperc);

std::set<uint160> ClosedPBaaSChains({CCrossChainRPCData::GetChainID("RESERVEWITHPREMINE"), CCrossChainRPCData::GetChainID("FAILCHAIN"), CCrossChainRPCData::GetChainID("SLC"), CCrossChainRPCData::GetChainID("RNPM"), CCrossChainRPCData::GetChainID("RNPNMR"), CCrossChainRPCData::GetChainID("MONKINS")});

// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid())
    {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {};

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn) {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

bool GetChainDefinition(uint160 chainID, CPBaaSChainDefinition &chainDef, int32_t *pDefHeight)
{
    LOCK(cs_main);
    if (chainID == ConnectedChains.ThisChain().GetChainID())
    {
        chainDef = ConnectedChains.ThisChain();
        if (pDefHeight)
        {
            *pDefHeight = 0;
        }
        return true;
    }

    if (!IsVerusActive())
    {
        if (ConnectedChains.NotaryChain().IsValid() && (chainID == ConnectedChains.NotaryChain().chainDefinition.GetChainID()))
        {
            chainDef = ConnectedChains.NotaryChain().chainDefinition;
            if (pDefHeight)
            {
                *pDefHeight = 0;
            }
            return true;
        }
    }

    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    bool found = false;

    if (!ClosedPBaaSChains.count(chainID)  && GetAddressIndex(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION)), 1, addressIndex))
    {
        for (auto txidx : addressIndex)
        {
            CTransaction tx;
            uint256 blkHash;
            if (GetTransaction(txidx.first.txhash, tx, blkHash))
            {
                chainDef = CPBaaSChainDefinition(tx);
                if (found = chainDef.IsValid() && chainDef.GetChainID() == chainID)
                {
                    // TODO: if we need to calculate the total contribution, do so
                    // we can get it from either a total of all exports, or the block 1 notarization if there is one
                    if (pDefHeight)
                    {
                        auto it = mapBlockIndex.find(blkHash);
                        *pDefHeight = it->second->GetHeight();
                    }
                    break;
                }
            }
        }
    }
    return found;
}

bool GetChainDefinition(string &name, CPBaaSChainDefinition &chainDef)
{
    return GetChainDefinition(CCrossChainRPCData::GetChainID(name), chainDef);
}

void GetDefinedChains(vector<CPBaaSChainDefinition> &chains, bool includeExpired)
{
    CCcontract_info CC;
    CCcontract_info *cp;

    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

    if (GetAddressIndex(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION)), 1, addressIndex))
    {
        for (auto txidx : addressIndex)
        {
            CTransaction tx;
            uint256 blkHash;
            if (GetTransaction(txidx.first.txhash, tx, blkHash))
            {
                chains.push_back(CPBaaSChainDefinition(tx));

                UniValue valStr(UniValue::VSTR);

                // TODO: remove/comment this if statement, as it is redundant with the one below
                if (!valStr.read(chains.back().ToUniValue().write()))
                {
                    printf("Invalid characters in blockchain definition: %s\n", chains.back().ToUniValue().write().c_str());
                }

                // remove after to use less storage
                if (!valStr.read(chains.back().ToUniValue().write()) || ClosedPBaaSChains.count(chains.back().GetChainID()) || (!includeExpired && chains.back().endBlock != 0 && chains.back().endBlock < chainActive.Height()))
                {
                    chains.pop_back();
                }
            }
        }
    }
}

bool CConnectedChains::GetLastImport(const uint160 &chainID, 
                                     CTransaction &lastImport, 
                                     CTransaction &crossChainExport, 
                                     CCrossChainImport &ccImport, 
                                     CCrossChainExport &ccCrossExport)
{
    CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_IMPORT);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    LOCK2(cs_main, mempool.cs);

    // get last import from the specified chain
    if (!GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        return false;
    }
    
    // make sure it isn't just a burned transaction to that address, drop out on first match
    const std::pair<CAddressUnspentKey, CAddressUnspentValue> *pOutput = NULL;
    COptCCParams p;
    for (const auto &output : unspentOutputs)
    {
        if (output.second.script.IsPayToCryptoCondition(p) && p.IsValid() && p.evalCode == EVAL_CROSSCHAIN_IMPORT)
        {
            pOutput = &output;
            break;
        }
    }
    if (!pOutput)
    {
        return false;
    }
    uint256 hashBlk;
    if (!myGetTransaction(pOutput->first.txhash, lastImport, hashBlk) || !(lastImport.vout.size() && lastImport.vout.back().scriptPubKey.IsOpReturn()))
    {
        return false;
    }
    ccImport = CCrossChainImport(p.vData[0]);
    auto opRetArr = RetrieveOpRetArray(lastImport.vout.back().scriptPubKey);
    if (!opRetArr.size() || opRetArr[0]->objectType != CHAINOBJ_TRANSACTION)
    {
        DeleteOpRetObjects(opRetArr);
    }
    else
    {
        crossChainExport = ((CChainObject<CTransaction> *)opRetArr[0])->object;
        DeleteOpRetObjects(opRetArr);
        if (!(ccCrossExport = CCrossChainExport(crossChainExport)).IsValid())
        {
            return false;
        }
    }
    return true;
}

// returns newly created import transactions to the specified chain from exports on this chain specified chain
// nHeight is the height for which we have an MMR that the chainID chain considers confirmed for this chain. it will
// accept proofs of any transactions with that MMR and height.
// Parameters shuold be validated before this call.
bool CConnectedChains::CreateLatestImports(const CPBaaSChainDefinition &chainDef, 
                                           const CTransaction &lastCrossChainImport, 
                                           const CTransaction &importTxTemplate,
                                           const CTransaction &lastConfirmedNotarization,
                                           CAmount totalAvailableInput,
                                           std::vector<CTransaction> &newImports)
{
    uint160 chainID = chainDef.GetChainID();

    CPBaaSNotarization lastConfirmed(lastConfirmedNotarization);
    if (!lastConfirmed.IsValid() || (chainActive.LastTip() == NULL) || lastConfirmed.notarizationHeight > chainActive.LastTip()->GetHeight())
    {
        LogPrintf("%s: Invalid lastConfirmedNotarization transaction\n", __func__);
        printf("%s: Invalid lastConfirmedNotarization transaction\n", __func__);
        return false;
    }

    CCrossChainImport lastCCI(lastCrossChainImport);
    if (!lastCCI.IsValid())
    {
        LogPrintf("%s: Invalid lastCrossChainImport transaction\n", __func__);
        printf("%s: Invalid lastCrossChainImport transaction\n", __func__);
        return false;
    }

    std::vector<CBaseChainObject *> chainObjs;
    // either a fully valid import with an export or the first import either in block 1 or the chain definition
    // on the Verus chain
    if (!(lastCrossChainImport.vout.back().scriptPubKey.IsOpReturn() &&
          (chainObjs = RetrieveOpRetArray(lastCrossChainImport.vout.back().scriptPubKey)).size() >= 2 &&
          chainObjs[0]->objectType == CHAINOBJ_TRANSACTION &&
          chainObjs[1]->objectType == CHAINOBJ_CROSSCHAINPROOF) &&
        !(chainObjs.size() == 0 && CPBaaSChainDefinition(lastCrossChainImport).IsValid()))
    {
        DeleteOpRetObjects(chainObjs);
        LogPrintf("%s: Invalid last import tx\n", __func__);
        printf("%s: Invalid last import tx\n", __func__);
        return false;
    }

    bool isVerusActive = IsVerusActive();

    LOCK2(cs_main, mempool.cs);

    uint256 lastExportHash;
    CTransaction lastExportTx;
    CTransaction tx;
    uint256 blkHash;
    uint32_t blkHeight = 0;
    bool found = false;
    if (chainObjs.size())
    {
        lastExportTx = ((CChainObject<CTransaction> *)chainObjs[0])->object;
        lastExportHash = lastExportTx.GetHash();
        BlockMap::iterator blkMapIt;
        DeleteOpRetObjects(chainObjs);
        if (myGetTransaction(lastExportHash, tx, blkHash) &&
            (blkMapIt = mapBlockIndex.find(blkHash)) != mapBlockIndex.end() && 
            blkMapIt->second)
        {
            found = true;
            blkHeight = blkMapIt->second->GetHeight();
        }
    }
    else
    {
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

        // we cannot get export to a chain that has shut down
        // if the chain definition is spent, a chain is inactive
        if (GetAddressUnspent(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION)), 1, unspentOutputs) && unspentOutputs.size())
        {
            CPBaaSChainDefinition localChainDef;

            for (auto txidx : unspentOutputs)
            {
                COptCCParams p;
                if (txidx.second.script.IsPayToCryptoCondition(p) && 
                    p.IsValid() && 
                    p.evalCode == EVAL_PBAASDEFINITION && 
                    p.vData[0].size() && 
                    (localChainDef = CPBaaSChainDefinition(p.vData[0])).IsValid() &&
                    ((isVerusActive && localChainDef.GetChainID() == chainID) || 
                         (!isVerusActive && localChainDef.GetChainID() == thisChain.GetChainID() && chainID == notaryChain.GetChainID())))
                {
                    if (myGetTransaction(txidx.first.txhash, tx, blkHash))
                    {
                        CCrossChainExport ccx(tx);
                        if (ccx.IsValid() && (isVerusActive || tx.IsCoinBase()))
                        {
                            found = true;
                            lastExportTx = tx;
                            lastExportHash = txidx.first.txhash;
                            blkHeight = txidx.second.blockHeight;
                        }
                    }
                }
            }
        }
    }
    if (!found)
    {
        LogPrintf("%s: No export thread found\n", __func__);
        printf("%s: No export thread found\n", __func__);
        return false;
    }

    bool importToReserveChain = !isVerusActive && chainID != thisChain.GetChainID() && chainID == notaryChain.GetChainID();

    // which transaction are we in this block?
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

    // look for new exports
    CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_EXPORT);

    CBlockIndex *pIndex;

    // get all export transactions including and since this one up to the confirmed height
    if (blkHeight <= lastConfirmed.notarizationHeight && GetAddressIndex(keyID, 1, addressIndex, blkHeight, lastConfirmed.notarizationHeight))
    {
        // find this export, then check the next one that spends it and use it if still valid
        bool found = false;
        uint256 lastHash = lastExportHash;

        // indexed by input hash
        std::map<uint256, std::pair<CAddressIndexKey, CTransaction>> validExports;

        // validate, order, and relate them with their inputs
        for (auto utxo : addressIndex)
        {
            // if current tx spends lastHash, then we have our next valid transaction to create an import with
            CTransaction tx, inputtx;
            uint256 blkHash1, blkHash2;
            BlockMap::iterator blkIt;
            CCrossChainExport ccx;
            COptCCParams p;
            if (!utxo.first.spending &&
                !(utxo.first.txhash == lastExportHash) &&
                myGetTransaction(utxo.first.txhash, tx, blkHash1) &&
                (ccx = CCrossChainExport(tx)).IsValid() &&
                ccx.numInputs &&
                (!tx.IsCoinBase() && 
                tx.vin.size() && 
                myGetTransaction(tx.vin[0].prevout.hash, inputtx, blkHash2)) &&
                inputtx.vout[tx.vin[0].prevout.n].scriptPubKey.IsPayToCryptoCondition(p) && 
                p.IsValid() && 
                p.evalCode == EVAL_CROSSCHAIN_EXPORT)
            {
                validExports.insert(make_pair(tx.vin[0].prevout.hash, make_pair(utxo.first, tx)));
            }
        }

        CTransaction lastImport(lastCrossChainImport);
        CMutableTransaction newImportTx(importTxTemplate);

        for (auto aixIt = validExports.find(lastExportHash); 
             aixIt != validExports.end(); 
             aixIt = validExports.find(lastExportHash))
        {
            // One pass - create an import transaction that spends the last import transaction from a confirmed export transaction that spends the last one of those
            // 1. Creates preconvert outputs that spend from the initial supply, which comes from the import transaction thread without fees
            // 2. Creates reserve outputs for unconverted imports
            // 3. Creates reserveExchange transactions requesting conversion at market for convert transfers
            // 4. Creates reserveTransfer outputs for outputs with the SEND_BACK flag set, unless they are under 5x the normal network fee
            // 5. Creates a pass-through EVAL_CROSSCHAIN_IMPORT output with the remainder of the non-preconverted coins
            // then signs the transaction considers it the latest import and the new export the latest export and
            // loops until there are no more confirmed, consecutive, valid export transactions to export

            // aixIt has an input from the export thread of last transaction, an optional deposit to the reserve, and an opret of all outputs + 1 fee
            assert(aixIt->second.second.vout.back().scriptPubKey.IsOpReturn());

            CCrossChainExport ccx(aixIt->second.second);
            lastCCI = CCrossChainImport(lastImport);
            if (!lastCCI.IsValid() || !ccx.IsValid())
            {
                LogPrintf("%s: POSSIBLE CORRUPTION bad import/export data in transaction %s (import) or %s (export)\n", __func__, lastImport.GetHash().GetHex().c_str(), aixIt->first.GetHex().c_str());
                printf("%s: POSSIBLE CORRUPTION bad import/export data transaction %s (import) or %s (export)\n", __func__, lastImport.GetHash().GetHex().c_str(), aixIt->first.GetHex().c_str());
                return false;
            }

            // if no prepared input, make one
            if (!newImportTx.vin.size())
            {
                newImportTx.vin.push_back(CTxIn(lastImport.GetHash(), 0));          // must spend output 0
            }
            newImportTx.vout.clear();
            newImportTx.vout.push_back(CTxOut()); // placeholder for the first output

            // emit a reserve exchange output
            // we will send using a reserve output, fee will be paid by converting from reserve
            CCcontract_info CC;
            CCcontract_info *cp;
            CPubKey pk;

            CAmount availableReserveFees = ccx.totalFees;
            CAmount exportFees = ccx.CalculateExportFee();
            CAmount importFees = ccx.CalculateImportFee();
            CAmount feesOut = 0;

            CReserveTransactionDescriptor rtxd;

            std::vector<CBaseChainObject *> exportOutputs = RetrieveOpRetArray(aixIt->second.second.vout.back().scriptPubKey);

            if (!rtxd.AddReserveTransferImportOutputs(chainDef, exportOutputs, newImportTx.vout))
            {
                LogPrintf("%s: POSSIBLE CORRUPTION bad export opret in transaction %s\n", __func__, aixIt->second.second.GetHash().GetHex().c_str());
                printf("%s: POSSIBLE CORRUPTION bad export opret in transaction %s\n", __func__, aixIt->second.second.GetHash().GetHex().c_str());
                // free the memory
                DeleteOpRetObjects(exportOutputs);
                return false;
            }

            // free the memory
            DeleteOpRetObjects(exportOutputs);

            // emit a crosschain import output as summary
            cp = CCinit(&CC, EVAL_CROSSCHAIN_IMPORT);

            pk = CPubKey(ParseHex(CC.CChexstr));

            if (rtxd.ReserveFees() != (ccx.totalFees - exportFees))
            {
                LogPrintf("%s: ERROR - import does not match amount, rtxd.ReserveFees()=%lu, totalImport=%lu, importFees=%lu, exportFees=%lu, ccx.totalAmount=%lu, ccx.totalFees=%lu\n", __func__, rtxd.ReserveFees(), ccx.totalAmount + ccx.totalFees, importFees, exportFees, ccx.totalAmount, ccx.totalFees);
                printf("%s: ERROR - import does not match amount, rtxd.ReserveFees()=%lu, totalImport=%lu, importFees=%lu, exportFees=%lu, ccx.totalAmount=%lu, ccx.totalFees=%lu\n", __func__, rtxd.ReserveFees(), ccx.totalAmount + ccx.totalFees, importFees, exportFees, ccx.totalAmount, ccx.totalFees);
            }

            std::vector<CTxDestination> dests = std::vector<CTxDestination>({CTxDestination(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_CROSSCHAIN_IMPORT)))});
            CCrossChainImport cci = CCrossChainImport(ConnectedChains.ThisChain().GetChainID(), ccx.totalAmount + ccx.totalFees);

            totalAvailableInput -= (importToReserveChain ? rtxd.reserveIn : rtxd.nativeIn);
            newImportTx.vout[0] = MakeCC1of1Vout(EVAL_CROSSCHAIN_IMPORT, totalAvailableInput, pk, dests, cci);

            if (totalAvailableInput < 0)
            {
                LogPrintf("%s: ERROR - importing more native currency than available on import thread for %s\n", __func__, chainDef.name.c_str());
                printf("%s: ERROR - importing more native currency than available on import thread for %s\n", __func__, chainDef.name.c_str());
                return false;
            }

            // add the opret, which is the export transaction this imports
            std::vector<CBaseChainObject *> chainObjects;
            CChainObject<CTransaction> exportTx(CHAINOBJ_TRANSACTION, aixIt->second.second);
            chainObjects.push_back(&exportTx);

            // add a proof of the export transaction at the notarization height
            CBlock block;
            if (!ReadBlockFromDisk(block, chainActive[aixIt->second.first.blockHeight], Params().GetConsensus(), false))
            {
                LogPrintf("%s: POSSIBLE CORRUPTION cannot read block %s\n", __func__, chainActive[lastConfirmed.notarizationHeight]->GetBlockHash().GetHex().c_str());
                printf("%s: POSSIBLE CORRUPTION cannot read block %s\n", __func__, chainActive[lastConfirmed.notarizationHeight]->GetBlockHash().GetHex().c_str());
                return false;
            }

            CMerkleBranch exportProof(aixIt->second.first.index, block.GetMerkleBranch(aixIt->second.first.index));
            auto mmrView = chainActive.GetMMV();
            mmrView.resize(lastConfirmed.notarizationHeight);

            if (mmrView.GetRoot() != lastConfirmed.mmrRoot)
            {
                LogPrintf("%s: notarization mmrRoot, %s, does not match the mmrRoot of the chain, %s\n", __func__, lastConfirmed.mmrRoot.GetHex().c_str(), mmrView.GetRoot().GetHex().c_str());
                printf("%s: notarization mmrRoot, %s, does not match the mmrRoot of the chain, %s\n", __func__, lastConfirmed.mmrRoot.GetHex().c_str(), mmrView.GetRoot().GetHex().c_str());
                return false;
            }
            else
            {
                printf("%s: created import %s for export %s\n", __func__, cci.ToUniValue().write().c_str(), aixIt->second.second.GetHash().GetHex().c_str());
            }


            chainActive[aixIt->second.first.blockHeight]->AddMerkleProofBridge(exportProof);
            mmrView.GetProof(exportProof, aixIt->second.first.blockHeight);

            CChainObject<CCrossChainProof> exportXProof(CHAINOBJ_CROSSCHAINPROOF, CCrossChainProof(lastConfirmedNotarization.GetHash(), exportProof));
            chainObjects.push_back(&exportXProof);

            // add the opret with the transaction and its proof that references the notarization with the correct MMR
            newImportTx.vout.push_back(CTxOut(0, StoreOpRetArray(chainObjects)));

            // we now have an Import transaction for the chainID chain, it is the latest, and the export we used is now the latest as well
            // work our way forward
            newImports.push_back(newImportTx);
            lastImport = newImportTx;
            newImportTx.vin.clear();
            newImportTx.vout.clear();
            lastExportHash = aixIt->second.first.txhash;
        }
    }
    return true;
}

void CheckPBaaSAPIsValid()
{
    if (!chainActive.LastTip() ||
        CConstVerusSolutionVector::activationHeight.ActiveVersion(chainActive.LastTip()->GetHeight()) < CConstVerusSolutionVector::activationHeight.ACTIVATE_PBAAS)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "PBaaS not activated on blockchain.");
    }
}

void CheckIdentityAPIsValid()
{
    if (!chainActive.LastTip() ||
        CConstVerusSolutionVector::activationHeight.ActiveVersion(chainActive.LastTip()->GetHeight()) < CConstVerusSolutionVector::activationHeight.ACTIVATE_IDENTITY)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Identity APIs not activated on blockchain.");
    }
}

uint160 GetChainIDFromParam(const UniValue &param)
{
    uint160 chainID;
    std::string name;

    if (param.isStr())
    {
        try
        {
            name = param.get_str();
            if (name.size() > KOMODO_ASSETCHAIN_MAXLEN - 1)
            {
                return chainID;
            }

            // if it's a 40 byte hex number, consider it a chainID, not a name, names should not be 40 byte hex numbers
            if (name.size() == 40)
            {
                chainID.SetHex(param.get_str());
            }
        }
        catch(const std::exception& e)
        {
        }

        if (chainID.IsNull())
        {
            chainID = CCrossChainRPCData::GetChainID(param.get_str());
        }
    }
    return chainID;
}

UniValue getchaindefinition(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getchaindefinition \"chainname\"\n"
            "\nReturns a complete definition for any given chain if it is registered on the blockchain. If the chain requested\n"
            "\nis NULL, chain definition of the current chain is returned.\n"

            "\nArguments\n"
            "1. \"chainname\"                     (string, optional) name of the chain to look for. no parameter returns current chain in daemon.\n"

            "\nResult:\n"
            "  {\n"
            "     \"chaindefinition\" : {\n"
            "        \"version\" : \"n\",             (int) version of this chain definition\n"
            "        \"name\" : \"string\",           (string) name or symbol of the chain, same as passed\n"
            "        \"address\" : \"string\",        (string) cryptocurrency address to send fee and non-converted premine\n"
            "        \"chainid\" : \"hex-string\",    (string) 40 char string that represents the chain ID, calculated from the name\n"
            "        \"premine\" : \"n\",             (int) amount of currency paid out to the premine address in block #1, may be smart distribution\n"
            "        \"convertible\" : \"xxxx\"       (bool) if this currency is a fractional reserve currency of Verus\n"
            "        \"launchfee\" : \"n\",           (int) (launchfee * total converted) / 100000000 sent directly to premine address\n"
            "        \"startblock\" : \"n\",          (int) block # on this chain, which must be notarized into block one of the chain\n"
            "        \"endblock\" : \"n\",            (int) block # after which, this chain's useful life is considered to be over\n"
            "        \"eras\" : \"[obj, ...]\",       (objarray) different chain phases of rewards and convertibility\n"
            "        {\n"
            "          \"reward\" : \"[n, ...]\",     (int) reward start for each era in native coin\n"
            "          \"decay\" : \"[n, ...]\",      (int) exponential or linear decay of rewards during each era\n"
            "          \"halving\" : \"[n, ...]\",    (int) blocks between halvings during each era\n"
            "          \"eraend\" : \"[n, ...]\",     (int) block marking the end of each era\n"
            "          \"eraoptions\" : \"[n, ...]\", (int) options for each era (reserved)\n"
            "        }\n"
            "        \"nodes\"      : \"[obj, ..]\",  (objectarray, optional) up to 2 nodes that can be used to connect to the blockchain"
            "          [{\n"
            "             \"nodeaddress\" : \"txid\", (string,  optional) internet, TOR, or other supported address for node\n"
            "             \"paymentaddress\" : \"n\", (int,     optional) rewards payment address\n"
            "           }, .. ]\n"
            "      }\n"
            "    \"bestnotarization\" : {\n"
            "     }\n"
            "    \"besttxid\" : \"txid\"\n"
            "     }\n"
            "    \"confirmednotarization\" : {\n"
            "     }\n"
            "    \"confirmedtxid\" : \"txid\"\n"
            "  }\n"

            "\nExamples:\n"
            + HelpExampleCli("getchaindefinition", "\"chainname\"")
            + HelpExampleRpc("getchaindefinition", "\"chainname\"")
        );
    }

    CheckPBaaSAPIsValid();

    UniValue ret(UniValue::VOBJ);

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CPBaaSChainDefinition chainDef;

    LOCK(cs_main);

    if (GetChainDefinition(chainID, chainDef))
    {
        CChainNotarizationData cnd;
        GetNotarizationData(chainID, IsVerusActive() ? EVAL_ACCEPTEDNOTARIZATION : EVAL_EARNEDNOTARIZATION, cnd);
        ret = chainDef.ToUniValue();

        int32_t confirmedHeight = -1, bestHeight = -1;
        confirmedHeight = cnd.vtx.size() && cnd.lastConfirmed != -1 ? cnd.vtx[cnd.lastConfirmed].second.notarizationHeight : -1;
        bestHeight = cnd.vtx.size() && cnd.bestChain != -1 ? cnd.vtx[cnd.forks[cnd.bestChain].back()].second.notarizationHeight : -1;

        ret.push_back(Pair("lastconfirmedheight", confirmedHeight == -1 ? 0 : confirmedHeight));
        if (confirmedHeight != -1)
        {
            ret.push_back(Pair("lastconfirmedtxid", cnd.vtx[cnd.lastConfirmed].first.GetHex().c_str()));
            ret.push_back(Pair("lastconfirmedcurrencystate", cnd.vtx[cnd.lastConfirmed].second.currencyState.ToUniValue()));
        }
        ret.push_back(Pair("bestheight", bestHeight == -1 ? 0 : bestHeight));
        if (bestHeight != -1)
        {
            ret.push_back(Pair("besttxid", cnd.vtx[cnd.forks[cnd.bestChain].back()].first.GetHex().c_str()));
            ret.push_back(Pair("bestcurrencystate", cnd.vtx[cnd.forks[cnd.bestChain].back()].second.currencyState.ToUniValue()));
        }
        return ret;
    }
    else
    {
        return NullUniValue;
    }
}

UniValue getpendingchaintransfers(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getpendingchaintransfers \"chainname\"\n"
            "\nReturns all pending transfers for a particular chain that have not yet been aggregated into an export\n"

            "\nArguments\n"
            "1. \"chainname\"                     (string, optional) name of the chain to look for. no parameter returns current chain in daemon.\n"

            "\nResult:\n"
            "  {\n"
            "  }\n"

            "\nExamples:\n"
            + HelpExampleCli("getpendingchaintransfers", "\"chainname\"")
            + HelpExampleRpc("getpendingchaintransfers", "\"chainname\"")
        );
    }

    CheckPBaaSAPIsValid();

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CPBaaSChainDefinition chainDef;
    int32_t defHeight;

    LOCK(cs_main);

    if ((IsVerusActive() && GetChainDefinition(chainID, chainDef, &defHeight)) || (chainDef = ConnectedChains.NotaryChain().chainDefinition).GetChainID() == chainID)
    {
        // look for new exports
        multimap<uint160, pair<CInputDescriptor, CReserveTransfer>> inputDescriptors;

        if (GetUnspentChainTransfers(inputDescriptors, chainID))
        {
            UniValue ret(UniValue::VARR);

            for (auto &desc : inputDescriptors)
            {
                COptCCParams p;
                if (desc.second.first.scriptPubKey.IsPayToCryptoCondition(p))
                {
                    UniValue oneExport(UniValue::VOBJ);

                    oneExport.push_back(Pair("chainid", desc.first.GetHex()));
                    oneExport.push_back(Pair("txid", desc.second.first.txIn.prevout.hash.GetHex()));
                    oneExport.push_back(Pair("n", (int32_t)desc.second.first.txIn.prevout.n));
                    oneExport.push_back(Pair("valueout", desc.second.first.nValue));
                    oneExport.push_back(Pair("reservetransfer", desc.second.second.ToUniValue()));
                    ret.push_back(oneExport);
                }
            }
            if (ret.size())
            {
                return ret;
            }
        }
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unrecognized chain name or chain ID");
    }
    
    return NullUniValue;
}

UniValue getchainexports(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getchainexports \"chainname\"\n"
            "\nReturns all pending export transfers that are not yet provable with confirmed notarizations.\n"

            "\nArguments\n"
            "1. \"chainname\"                     (string, optional) name of the chain to look for. no parameter returns current chain in daemon.\n"

            "\nResult:\n"
            "  {\n"
            "  }\n"

            "\nExamples:\n"
            + HelpExampleCli("getchainexports", "\"chainname\"")
            + HelpExampleRpc("getchainexports", "\"chainname\"")
        );
    }

    CheckPBaaSAPIsValid();

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CPBaaSChainDefinition chainDef;
    int32_t defHeight;

    LOCK(cs_main);

    if ((IsVerusActive() && GetChainDefinition(chainID, chainDef, &defHeight)) || (chainDef = ConnectedChains.NotaryChain().chainDefinition).GetChainID() == chainID)
    {
        // which transaction are we in this block?
        std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

        // look for new exports
        CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_EXPORT);

        CChainNotarizationData cnd;
        if (GetNotarizationData(chainID, IsVerusActive() ? EVAL_ACCEPTEDNOTARIZATION : EVAL_EARNEDNOTARIZATION, cnd))
        {
            // get all export transactions including and since this one up to the confirmed height
            if (GetAddressIndex(keyID, 1, addressIndex, cnd.IsConfirmed() ? cnd.vtx[cnd.lastConfirmed].second.crossHeight : defHeight))
            {
                UniValue ret(UniValue::VARR);

                for (auto &idx : addressIndex)
                {
                    uint256 blkHash;
                    CTransaction exportTx;
                    if (!idx.first.spending && myGetTransaction(idx.first.txhash, exportTx, blkHash))
                    {
                        std::vector<CBaseChainObject *> opretTransfers;
                        CCrossChainExport ccx;
                        if ((ccx = CCrossChainExport(exportTx)).IsValid() && exportTx.vout.back().scriptPubKey.IsOpReturn())
                        {
                            UniValue oneExport(UniValue::VOBJ);
                            UniValue transferArray(UniValue::VARR);
                            opretTransfers = RetrieveOpRetArray(exportTx.vout.back().scriptPubKey);
                            oneExport.push_back(Pair("blockheight", idx.first.blockHeight));
                            oneExport.push_back(Pair("exportid", idx.first.txhash.GetHex()));
                            oneExport.push_back(Pair("description", ccx.ToUniValue()));
                            for (auto oneTransfer : opretTransfers)
                            {
                                if (oneTransfer->objectType == CHAINOBJ_RESERVETRANSFER)
                                {
                                    transferArray.push_back(((CChainObject<CReserveTransfer> *)oneTransfer)->object.ToUniValue());
                                }
                            }
                            DeleteOpRetObjects(opretTransfers);
                            oneExport.push_back(Pair("transfers", transferArray));
                            ret.push_back(oneExport);
                        }
                    }
                }
                if (ret.size())
                {
                    return ret;
                }
            }
        }
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unrecognized chain name or chain ID");
    }
    
    return NullUniValue;
}

UniValue getchainimports(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getchainimports \"chainname\"\n"
            "\nReturns all imports from a specific chain.\n"

            "\nArguments\n"
            "1. \"chainname\"                     (string, optional) name of the chain to look for. no parameter returns current chain in daemon.\n"

            "\nResult:\n"
            "  {\n"
            "  }\n"

            "\nExamples:\n"
            + HelpExampleCli("getchainimports", "\"chainname\"")
            + HelpExampleRpc("getchainimports", "\"chainname\"")
        );
    }

    CheckPBaaSAPIsValid();

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CPBaaSChainDefinition chainDef;
    int32_t defHeight;

    LOCK(cs_main);

    if ((IsVerusActive() && GetChainDefinition(chainID, chainDef, &defHeight)) || (chainDef = ConnectedChains.NotaryChain().chainDefinition).GetChainID() == chainID)
    {
        // which transaction are we in this block?
        std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

        // look for new exports
        CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_IMPORT);

        CBlockIndex *pIndex;

        CChainNotarizationData cnd;
        // get all export transactions including and since this one up to the confirmed height
        if (GetAddressIndex(keyID, 1, addressIndex))
        {
            UniValue ret(UniValue::VARR);

            for (auto &idx : addressIndex)
            {
                uint256 blkHash;
                CTransaction importTx;
                if (!idx.first.spending && myGetTransaction(idx.first.txhash, importTx, blkHash))
                {
                    CCrossChainImport cci;
                    if ((cci = CCrossChainImport(importTx)).IsValid() && importTx.vout.back().scriptPubKey.IsOpReturn())
                    {
                        std::vector<CBaseChainObject *> opretImports;
                        CTransaction exportTx;
                        CCrossChainExport ccx;

                        opretImports = RetrieveOpRetArray(importTx.vout.back().scriptPubKey);

                        if (opretImports.size() >= 2 && 
                            opretImports[0]->objectType == CHAINOBJ_TRANSACTION && 
                            (ccx = CCrossChainExport(exportTx = ((CChainObject<CTransaction> *)opretImports[0])->object)).IsValid() && 
                            ccx.numInputs &&
                            exportTx.vout.size() && 
                            exportTx.vout.back().scriptPubKey.IsOpReturn())
                        {
                            std::vector<CBaseChainObject *> opretTransfers;

                            UniValue oneImport(UniValue::VOBJ);
                            UniValue transferArray(UniValue::VARR);
                            opretTransfers = RetrieveOpRetArray(exportTx.vout.back().scriptPubKey);
                            oneImport.push_back(Pair("blockheight", idx.first.blockHeight));
                            oneImport.push_back(Pair("importid", idx.first.txhash.GetHex()));
                            oneImport.push_back(Pair("description", cci.ToUniValue()));
                            for (auto oneTransfer : opretTransfers)
                            {
                                if (oneTransfer->objectType == CHAINOBJ_RESERVETRANSFER)
                                {
                                    transferArray.push_back(((CChainObject<CReserveTransfer> *)oneTransfer)->object.ToUniValue());
                                }
                            }
                            DeleteOpRetObjects(opretTransfers);
                            oneImport.push_back(Pair("transfers", transferArray));
                            ret.push_back(oneImport);
                        }
                        DeleteOpRetObjects(opretImports);
                    }
                }
            }
            if (ret.size())
            {
                return ret;
            }
        }
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unrecognized chain name or chain ID");
    }
    return NullUniValue;
}

UniValue getdefinedchains(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getdefinedchains (includeexpired)\n"
            "\nReturns a complete definition for any given chain if it is registered on the blockchain. If the chain requested\n"
            "\nis NULL, chain definition of the current chain is returned.\n"

            "\nArguments\n"
            "1. \"includeexpired\"                (bool, optional) if true, include chains that are no longer active\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\" : \"n\",             (int) version of this chain definition\n"
            "    \"name\" : \"string\",           (string) name or symbol of the chain, same as passed\n"
            "    \"address\" : \"string\",        (string) cryptocurrency address to send fee and non-converted premine\n"
            "    \"chainid\" : \"hex-string\",    (string) 40 char string that represents the chain ID, calculated from the name\n"
            "    \"premine\" : \"n\",             (int) amount of currency paid out to the premine address in block #1, may be smart distribution\n"
            "    \"convertible\" : \"xxxx\"       (bool) if this currency is a fractional reserve currency of Verus\n"
            "    \"launchfee\" : \"n\",           (int) (launchfee * total converted) / 100000000 sent directly to premine address\n"
            "    \"startblock\" : \"n\",          (int) block # on this chain, which must be notarized into block one of the chain\n"
            "    \"endblock\" : \"n\",            (int) block # after which, this chain's useful life is considered to be over\n"
            "    \"eras\" : \"[obj, ...]\",       (objarray) different chain phases of rewards and convertibility\n"
            "    {\n"
            "      \"reward\" : \"[n, ...]\",     (int) reward start for each era in native coin\n"
            "      \"decay\" : \"[n, ...]\",      (int) exponential or linear decay of rewards during each era\n"
            "      \"halving\" : \"[n, ...]\",    (int) blocks between halvings during each era\n"
            "      \"eraend\" : \"[n, ...]\",     (int) block marking the end of each era\n"
            "      \"eraoptions\" : \"[n, ...]\", (int) options for each era (reserved)\n"
            "    }\n"
            "    \"nodes\"      : \"[obj, ..]\",  (objectarray, optional) up to 2 nodes that can be used to connect to the blockchain"
            "      [{\n"
            "         \"nodeaddress\" : \"txid\", (string,  optional) internet, TOR, or other supported address for node\n"
            "         \"paymentaddress\" : \"n\", (int,     optional) rewards payment address\n"
            "       }, .. ]\n"
            "  }, ...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("getdefinedchains", "true")
            + HelpExampleRpc("getdefinedchains", "true")
        );
    }

    CheckPBaaSAPIsValid();

    UniValue ret(UniValue::VARR);

    bool includeExpired = params[0].isBool() ? params[0].get_bool() : false;

    vector<CPBaaSChainDefinition> chains;
    GetDefinedChains(chains, includeExpired);

    for (auto def : chains)
    {
        UniValue oneChain(UniValue::VOBJ);
        oneChain.push_back(Pair("chaindefinition", def.ToUniValue()));

        CChainNotarizationData cnd;
        GetNotarizationData(def.GetChainID(), IsVerusActive() ? EVAL_ACCEPTEDNOTARIZATION : EVAL_EARNEDNOTARIZATION, cnd);

        int32_t confirmedHeight = -1, bestHeight = -1;
        confirmedHeight = cnd.vtx.size() && cnd.lastConfirmed != -1 ? cnd.vtx[cnd.lastConfirmed].second.notarizationHeight : -1;
        bestHeight = cnd.vtx.size() && cnd.bestChain != -1 ? cnd.vtx[cnd.forks[cnd.bestChain].back()].second.notarizationHeight : -1;

        oneChain.push_back(Pair("lastconfirmedheight", confirmedHeight == -1 ? 0 : confirmedHeight));
        if (confirmedHeight != -1)
        {
            oneChain.push_back(Pair("lastconfirmedtxid", cnd.vtx[cnd.lastConfirmed].first.GetHex().c_str()));
            oneChain.push_back(Pair("lastconfirmedcurrencystate", cnd.vtx[cnd.lastConfirmed].second.currencyState.ToUniValue()));
        }
        oneChain.push_back(Pair("bestheight", bestHeight == -1 ? 0 : bestHeight));
        if (bestHeight != -1)
        {
            oneChain.push_back(Pair("besttxid", cnd.vtx[cnd.forks[cnd.bestChain].back()].first.GetHex().c_str()));
            oneChain.push_back(Pair("bestcurrencystate", cnd.vtx[cnd.forks[cnd.bestChain].back()].second.currencyState.ToUniValue()));
        }
        ret.push_back(oneChain);
    }
    return ret;
}

// returns all chain transfer outputs, both spent and unspent between a specific start and end block with an optional chainFilter. if the chainFilter is not
// NULL, only transfers to that chain are returned
bool GetChainTransfers(multimap<uint160, pair<CInputDescriptor, CReserveTransfer>> &inputDescriptors, uint160 chainFilter, int start, int end, uint32_t flags)
{
    if (!flags)
    {
        flags = CReserveTransfer::VALID;
    }
    bool nofilter = chainFilter.IsNull();
    CKeyID keyID;

    // look for unspent chain transfer outputs for all chains
    keyID = CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_RESERVE_TRANSFER);

    // which transaction are we in this block?
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    std::set<uint256> countedTxes;                  // don't count twice

    LOCK2(cs_main, mempool.cs);

    if (!GetAddressIndex(keyID, 1, addressIndex, start, end))
    {
        return false;
    }
    else
    {
        for (auto it = addressIndex.begin(); it != addressIndex.end(); it++)
        {
            CTransaction ntx;
            uint256 blkHash;

            // each tx gets counted once
            if (countedTxes.count(it->first.txhash))
            {
                continue;
            }
            countedTxes.insert(it->first.txhash);

            if (myGetTransaction(it->first.txhash, ntx, blkHash))
            {
                for (int i = 0; i < ntx.vout.size(); i++)
                {
                    // if this is a transfer output, optionally to this chain, add it to the input vector
                    COptCCParams p;
                    CReserveTransfer rt;
                    if (ntx.vout[i].scriptPubKey.IsPayToCryptoCondition(p) && 
                        p.evalCode == EVAL_RESERVE_TRANSFER &&
                        p.vData.size() && (rt = CReserveTransfer(p.vData[0])).IsValid() &&
                        p.vKeys.size() > 1 &&
                        (nofilter || GetDestinationID(p.vKeys[1]) == chainFilter) &&
                        (rt.flags & flags) == flags)
                    {
                        inputDescriptors.insert(make_pair(GetDestinationID(p.vKeys[1]),
                                                    make_pair(CInputDescriptor(ntx.vout[i].scriptPubKey, ntx.vout[i].nValue, CTxIn(COutPoint(it->first.txhash, i))), rt)));
                    }
                }
            }
            else
            {
                LogPrintf("%s: cannot retrieve transaction %s\n", __func__, it->first.txhash.GetHex().c_str());
                printf("%s: cannot retrieve transaction %s\n", __func__, it->first.txhash.GetHex().c_str());
                return false;
            }
        }
        return true;
    }
}

// returns all unspent chain transfer outputs with an optional chainFilter. if the chainFilter is not
// NULL, only transfers to that chain are returned
bool GetUnspentChainTransfers(multimap<uint160, pair<CInputDescriptor, CReserveTransfer>> &inputDescriptors, uint160 chainFilter)
{
    bool nofilter = chainFilter.IsNull();
    CKeyID keyID;

    // look for unspent chain transfer outputs for all chains
    keyID = CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_RESERVE_TRANSFER);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    LOCK2(cs_main, mempool.cs);

    if (!GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        return false;
    }
    else
    {
        CCoinsViewCache view(pcoinsTip);

        for (auto it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
        {
            CCoins coins;

            if (view.GetCoins(it->first.txhash, coins))
            {
                for (int i = 0; i < coins.vout.size(); i++)
                {
                    if (coins.IsAvailable(i))
                    {
                        // if this is a transfer output, optionally to this chain, add it to the input vector
                        // chain filter was applied in index search
                        COptCCParams p;
                        CReserveTransfer rt;
                        if (::IsPayToCryptoCondition(coins.vout[i].scriptPubKey, p) && p.evalCode == EVAL_RESERVE_TRANSFER &&
                            p.vData.size() && (rt = CReserveTransfer(p.vData[0])).IsValid() &&
                            p.vKeys.size() > 1 &&
                            (nofilter || GetDestinationID(p.vKeys[1]) == chainFilter))
                        {
                            inputDescriptors.insert(make_pair(GetDestinationID(p.vKeys[1]),
                                                     make_pair(CInputDescriptor(coins.vout[i].scriptPubKey, coins.vout[i].nValue, CTxIn(COutPoint(it->first.txhash, i))), rt)));
                        }
                    }
                }
            }
            else
            {
                printf("%s: cannot retrieve transaction %s\n", __func__, it->first.txhash.GetHex().c_str());
            }
        }
        return true;
    }
}

// returns all unspent chain transfer outputs with an optional chainFilter. if the chainFilter is not
// NULL, only transfers to that chain are returned
bool GetUnspentChainExports(uint160 chainID, multimap<uint160, pair<int, CInputDescriptor>> &exportOutputs)
{
    // look for unspent notarization finalization outputs for the requested chain
    CKeyID keyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_EXPORT));

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    LOCK2(cs_main, mempool.cs);

    if (!GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        return false;
    }
    else
    {
        CCoinsViewCache view(pcoinsTip);

        for (auto it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
        {
            CCoins coins;

            if (view.GetCoins(it->first.txhash, coins))
            {
                for (int i = 0; i < coins.vout.size(); i++)
                {
                    if (coins.IsAvailable(i))
                    {
                        // if this is an export output, optionally to this chain, add it to the input vector
                        COptCCParams p;
                        CCrossChainExport cx;
                        if (::IsPayToCryptoCondition(coins.vout[i].scriptPubKey, p) && p.evalCode == EVAL_CROSSCHAIN_EXPORT &&
                            p.vData.size() && (cx = CCrossChainExport(p.vData[0])).IsValid())
                        {
                            exportOutputs.insert(make_pair(cx.chainID,
                                                     make_pair(coins.nHeight, CInputDescriptor(coins.vout[i].scriptPubKey, coins.vout[i].nValue, CTxIn(COutPoint(it->first.txhash, i))))));
                        }
                    }
                }
            }
            else
            {
                printf("%s: cannot retrieve transaction %s\n", __func__, it->first.txhash.GetHex().c_str());
                return false;
            }
        }
        return true;
    }
}

bool GetNotarizationData(uint160 chainID, uint32_t ecode, CChainNotarizationData &notarizationData, vector<pair<CTransaction, uint256>> *optionalTxOut)
{
    notarizationData.version = PBAAS_VERSION;

    // look for unspent notarization finalization outputs for the requested chain
    CKeyID keyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_FINALIZENOTARIZATION));

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    CPBaaSChainDefinition chainDef;

    bool isEarned = ecode == EVAL_EARNEDNOTARIZATION;

    LOCK2(cs_main, mempool.cs);

    if (!GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        return false;
    }
    else
    {
        multimap<int32_t, pair<uint256, CPBaaSNotarization>> sorted;
        multimap<int32_t, pair<CTransaction, uint256>> sortedTxs;

        notarizationData.lastConfirmed = 0;

        // filter out all transactions that do not spend from the notarization thread, or originate as the
        // chain definition
        for (auto it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
        {
            // printf("txid: %s\n", it->first.txhash.GetHex().c_str());
            CTransaction ntx;
            uint256 blkHash;

            if (myGetTransaction(it->first.txhash, ntx, blkHash))
            {
                if (!chainDef.IsValid())
                {
                    // try to make a chain definition out of each transaction, and keep the first one that is valid
                    chainDef = CPBaaSChainDefinition(ntx);
                }
                CPBaaSNotarization notarization = CPBaaSNotarization(ntx);
                if (notarization.IsValid())
                {
                    auto blkit = mapBlockIndex.find(blkHash);
                    if (blkit != mapBlockIndex.end())
                    {
                        // sort by block height, index by transaction id
                        sorted.insert(make_pair(blkit->second->GetHeight(), make_pair(it->first.txhash, notarization)));
                        if (optionalTxOut)
                        {
                            sortedTxs.insert(make_pair(blkit->second->GetHeight(), make_pair(ntx, blkHash)));
                        }
                    }
                    // if we are not on block 1 of a PBaaS chain and we are a first notarization that is not confirmed, none can be confirmed yet
                    if (notarization.prevHeight == 0 && !isEarned)
                    {
                        notarizationData.lastConfirmed = -1;
                    }
                }
            }
            else
            {
                printf("cannot retrieve transaction %s, may need to reindex\n", it->first.txhash.GetHex().c_str());
                //return false;
            }
        }

        if (!sorted.size())
        {
            //printf("no notarizations found\n");
            return false;
        }

        if (!chainDef.IsValid())
        {
            // the first entry of all forks must reference a confirmed transaction if there is one
            CTransaction rootTx;
            uint256 blkHash;
            auto prevHash = sorted.begin()->second.second.prevNotarization;
            if (!prevHash.IsNull())
            {
                if (!myGetTransaction(prevHash, rootTx, blkHash))
                {
                    return false;
                }

                // ensure that we have a finalization output
                COptCCParams p;
                CPBaaSNotarization notarization;
                CNotarizationFinalization finalization;
                uint32_t notarizeIdx, finalizeIdx;

                if (GetNotarizationAndFinalization(ecode, CMutableTransaction(rootTx), notarization, &notarizeIdx, &finalizeIdx))
                {
                    notarizationData.vtx.insert(notarizationData.vtx.begin(), make_pair(prevHash, notarization));
                    notarizationData.lastConfirmed = 0;
                    if (optionalTxOut)
                    {
                        optionalTxOut->insert(optionalTxOut->begin(), make_pair(rootTx, blkHash));
                    }
                }
                // debugging, this else is not needed
                else
                {
                    printf("previous transaction does not have both notarization and finalizaton outputs\n");
                }
            }
            else
            {
                if (!isEarned)
                {
                    notarizationData.lastConfirmed = -1;
                }
            }
        }
        else
        {
            if (!isEarned)
            {
                // we still have the chain definition in our forks, so no notarization has been confirmed yet
                notarizationData.lastConfirmed = -1;
            }
        }

        multimap<uint256, pair<int32_t, int32_t>> references;       // associates the txid, the fork index, and the index in the fork

        for (auto p : sorted)
        {
            notarizationData.vtx.push_back(make_pair(p.second.first, p.second.second));
        }

        if (optionalTxOut)
        {
            for (auto p : sortedTxs)
            {
                optionalTxOut->push_back(p.second);
            }
        }

        // we now have all unspent notarizations sorted by block height, and the last confirmed notarization as first, if there
        // is one. if there is a confirmed notarization, all forks should refer to it, or they are invalid and should be spent.

        // find roots and create a chain from each
        for (int32_t i = 0; i < notarizationData.vtx.size(); i++)
        {
            auto &nzp = notarizationData.vtx[i];
            auto it = nzp.second.prevNotarization.IsNull() ? references.end() : references.find(nzp.second.prevNotarization);

            int32_t chainIdx = 0;
            int32_t posIdx = 0;

            // do we refer to a notarization that is already in a fork?
            if (it != references.end())
            {
                std::vector<int32_t> &fork = notarizationData.forks[it->second.first];

                // if it is the end of the fork, put this entry there, if not the end, copy up to it and start another fork
                if (it->second.second == (fork.size() - 1))
                {
                    fork.push_back(i);
                    chainIdx = it->second.first;
                    posIdx = fork.size() - 1;
                }
                else
                {
                    notarizationData.forks.push_back(vector<int32_t>(&fork[0], &fork[it->second.second] + 1));
                    notarizationData.forks.back().push_back(i);
                    chainIdx = notarizationData.forks.size() - 1;
                    posIdx = notarizationData.forks.back().size() - 1;
                }
            }
            else
            {
                // start a new fork that references no one else
                notarizationData.forks.push_back(vector<int32_t>(0));
                notarizationData.forks.back().push_back(i);
                chainIdx = notarizationData.forks.size() - 1;
                posIdx = notarizationData.forks.back().size() - 1;
            }
            references.insert(make_pair(nzp.first, make_pair(chainIdx, posIdx)));
        }

        CChainPower best;

        // now, we should have all forks in vectors
        // they should all have roots that point to the same confirmed or initial notarization, which should be enforced by chain rules
        // the best chain should simply be the tip with most power
        for (int i = 0; i < notarizationData.forks.size(); i++)
        {
            CChainPower curPower = ExpandCompactPower(notarizationData.vtx[notarizationData.forks[i].back()].second.compactPower, i);
            if (curPower > best)
            {
                best = curPower;
            }
        }
        notarizationData.bestChain = best.nHeight;
        return true;
    }
}

UniValue getnotarizationdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "getnotarizationdata \"chainid\" accepted\n"
            "\nReturns the latest PBaaS notarization data for the specifed chainid.\n"

            "\nArguments\n"
            "1. \"chainid\"                     (string, required) the hex-encoded ID or string name  search for notarizations on\n"
            "2. \"accepted\"                    (bool, optional) accepted, not earned notarizations, default: true if on\n"
            "                                                    VRSC or VRSCTEST, false otherwise\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                 (numeric) The notarization protocol version\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getnotarizationdata", "\"chainid\" true")
            + HelpExampleRpc("getnotarizationdata", "\"chainid\"")
        );
    }

    CheckPBaaSAPIsValid();

    uint160 chainID;
    CChainNotarizationData nData;
    uint32_t ecode;
    
    if (IsVerusActive())
    {
        ecode = EVAL_ACCEPTEDNOTARIZATION;
    }
    else
    {
        ecode = EVAL_EARNEDNOTARIZATION;
    }

    chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chainid");
    }

    if (params.size() > 1)
    {
        if (!params[1].get_bool())
        {
            ecode = EVAL_EARNEDNOTARIZATION;
        }
    }

    if (GetNotarizationData(chainID, ecode, nData))
    {
        return nData.ToUniValue();
    }
    else
    {
        return NullUniValue;
    }
}

// get inputs for all of the unspent reward outputs sent to a specific reward type for the range specified
// returns the total input amount
CAmount GetUnspentRewardInputs(const CPBaaSChainDefinition &chainDef, vector<CInputDescriptor> &inputs, int32_t serviceCode, int32_t height)
{
    CAmount retval = 0;
    uint160 chainID = chainDef.GetChainID();

    // look for unspent outputs that match the addressout hashed with service code
    CKeyID keyID(CCrossChainRPCData::GetConditionID(CCrossChainRPCData::GetConditionID(chainID, serviceCode), EVAL_SERVICEREWARD));

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if (GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        // spend all billing periods prior or equal to this one
        int billingPeriod = height / chainDef.billingPeriod;

        // we need to look through the inputs to ensure that they are
        // actual service reward outputs in the correct billing periof, since we don't currently prevent other types of transaction outputs from being
        // sent to the same address, though doing so would burn its value anyhow

        LOCK(cs_main);
        for (auto output : unspentOutputs)
        {
            // printf("txid: %s\n", it->first.txhash.GetHex().c_str());
            CServiceReward sr;
            CCoins coins;
            if (pcoinsTip->GetCoins(output.first.txhash, coins))
            {
                for (auto txout : coins.vout)
                {
                    COptCCParams p;
                    if (!txout.IsNull() && IsPayToCryptoCondition(txout.scriptPubKey, p) && p.evalCode == EVAL_SERVICEREWARD)
                    {
                        FromVector(p.vData[0], sr);
                        if (sr.IsValid())
                        {
                            inputs.push_back(CInputDescriptor(txout.scriptPubKey, txout.nValue, CTxIn(output.first.txhash, output.first.index)));
                            retval += txout.nValue;
                        }
                    }
                    else
                    {
                        LogPrintf("GetUnspentRewardInputs: cannot retrieve transaction %s\n", output.first.txhash.GetHex().c_str());
                        printf("GetUnspentRewardInputs: cannot retrieve transaction %s\n", output.first.txhash.GetHex().c_str());
                    }
                }
            }
        }
    }
    return retval;
}

// this adds any new notarization rewards that have been sent to the notarization reward pool for this
// billing period since last accepted notarization, up to a maximum number of inputs
CAmount AddNewNotarizationRewards(CPBaaSChainDefinition &chainDef, vector<CInputDescriptor> &inputs, CMutableTransaction mnewTx, int32_t height)
{
    // get current chain info
    CAmount newIn = 0;
    newIn = GetUnspentRewardInputs(chainDef, inputs, SERVICE_NOTARIZATION, height);
    for (auto input : inputs)
    {
        mnewTx.vin.push_back(input.txIn);
    }
    return newIn;
}

UniValue submitacceptednotarization(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "submitacceptednotarization \"hextx\"\n"
            "\nFinishes an almost complete notarization transaction based on the notary chain and the current wallet or pubkey.\n"
            "\nIf successful in submitting the transaction based on all rules, a transaction ID is returned, otherwise, NULL.\n"

            "\nArguments\n"
            "1. \"hextx\"                       (hexstring, required) partial hex-encoded notarization transaction to submit\n"
            "                                   transaction should have only one notarization and one opret output\n"

            "\nResult:\n"
            "txid                               (hexstring) transaction ID of submitted transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("submitacceptednotarization", "\"hextx\"")
            + HelpExampleRpc("submitacceptednotarization", "\"hextx\"")
        );
    }

    CheckPBaaSAPIsValid();

    // decode the transaction and ensure that it is formatted as expected
    CTransaction notarization;
    CPBaaSNotarization pbn;

    if (!DecodeHexTx(notarization, params[0].get_str()) || 
        notarization.vin.size() || 
        notarization.vout.size() != 2 ||
        !(pbn = CPBaaSNotarization(notarization)).IsValid() ||
        !notarization.vout.back().scriptPubKey.IsOpReturn())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid notarization transaction");
    }

    CPBaaSChainDefinition chainDef;
    int32_t chainDefHeight;
    if (!GetChainDefinition(pbn.chainID, chainDef, &chainDefHeight))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain notarization");
    }

    // ensure we are still eligible to submit
    // finalize all transactions we can and send the notarization reward, plus all orphaned finalization outputs
    // to the confirmed recipient

    CChainNotarizationData nData;
    vector<pair<CTransaction, uint256>> txesBlkHashes;

    bool success;
    
    {
        LOCK(cs_main);
        success = GetNotarizationData(pbn.chainID, EVAL_ACCEPTEDNOTARIZATION, nData, &txesBlkHashes);
    }

    // get notarization data and check all transactions
    if (success)
    {

        //LogPrintf("Accepted notarization GetNotarizationData returns %lu entries\n", nData.vtx.size());
        //printf("Accepted notarization GetNotarizationData returns %lu entries\n", nData.vtx.size());

        // if any notarization exists that is accepted and more recent than the last one, but one we still agree with,
        // we cannot submit, aside from that, we will prepare and submit
        set<uint256> priorBlocks;
        map<uint256, CPBaaSNotarization *> notarizationData;

        // printf("opRet: %s\n", notarization.vout[notarization.vout.size() - 1].scriptPubKey.ToString().c_str());

        auto chainObjects = RetrieveOpRetArray(notarization.vout[notarization.vout.size() - 1].scriptPubKey);

        bool stillValid = false;
        if (chainObjects.size() && chainObjects.back()->objectType == CHAINOBJ_PRIORBLOCKS)
        {
            // once here, default to true
            stillValid = true;

            CPriorBlocksCommitment &pbc = ((CChainObject<CPriorBlocksCommitment> *)chainObjects.back())->object;
            for (auto prior : pbc.priorBlocks)
            {
                priorBlocks.insert(prior);
            }

            for (auto it = nData.vtx.rbegin(); it != nData.vtx.rend(); it++)
            {
                // if any existing, accepted notarization is a subset of us, don't post, we will be rejected anyhow
                if (priorBlocks.count(it->second.notarizationPreHash))
                {
                    stillValid = false;
                    break;
                }
                else
                {
                    notarizationData.insert(make_pair(it->first, &it->second));
                }
            }
        }

        DeleteOpRetObjects(chainObjects);

        auto lastIt = notarizationData.find(pbn.prevNotarization);

        if (!stillValid || (lastIt == notarizationData.end()))
        {
            //printf("Notarization not matched or invalidated by prior notarization\n");
            throw JSONRPCError(RPC_VERIFY_REJECTED, "Notarization not matched or invalidated by prior notarization");
        }

        if (pbn.prevHeight != lastIt->second->notarizationHeight)
        {
            printf("Notarization heights not matched with previous notarization\n");
            throw JSONRPCError(RPC_VERIFY_REJECTED, "Notarization heights not matched with previous notarization");
        }

        if (pbn.prevHeight != 0 && (pbn.prevHeight + CPBaaSNotarization::MIN_BLOCKS_BETWEEN_ACCEPTED > pbn.notarizationHeight))
        {
            //printf("Less than minimum number of blocks between notarizations\n");
            throw JSONRPCError(RPC_VERIFY_REJECTED, "Less than minimum number of blocks between notarizations");
        }

        // valid, spend notarization outputs, all needed finalizations, add any applicable reward output for 
        // confirmation to confirmed notary and miner of block,
        // add our output address and submit
        CMutableTransaction mnewTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), chainActive.Height());
        vector<CInputDescriptor> notarizationInputs;
        for (auto input : notarization.vin)
        {
            mnewTx.vin.push_back(input);
        }
        for (auto output : notarization.vout)
        {
            mnewTx.vout.push_back(output);
        }

        CTransaction lastTx = txesBlkHashes.back().first;

        int32_t confirmedInput = -1;
        int32_t confirmedIndex;
        CTxDestination payee;

        uint32_t notarizationIdx = -1, finalizationIdx = -1;
        CPBaaSNotarization dummy;

        notarizationInputs = AddSpendsAndFinalizations(nData, pbn.prevNotarization, mnewTx, &confirmedInput, &confirmedIndex, &payee);

        // if we got our inputs, add finalization
        if (notarizationInputs.size())
        {
            CCcontract_info CC;
            CCcontract_info *cp;
            cp = CCinit(&CC, EVAL_FINALIZENOTARIZATION);

            // use public key of cc
            CPubKey pk(ParseHex(CC.CChexstr));
            CKeyID id = CCrossChainRPCData::GetConditionID(pbn.chainID, EVAL_FINALIZENOTARIZATION);
            std::vector<CTxDestination> dests({id});

            // insert a finalization as second to last vout
            cp = CCinit(&CC, EVAL_FINALIZENOTARIZATION);
            pk = CPubKey(ParseHex(CC.CChexstr));
            dests = std::vector<CTxDestination>({CKeyID(CCrossChainRPCData::GetConditionID(pbn.chainID, EVAL_FINALIZENOTARIZATION))});

            CNotarizationFinalization nf(confirmedInput);

            mnewTx.vout.insert(mnewTx.vout.begin() + (mnewTx.vout.size() - 1), MakeCC1of1Vout(EVAL_FINALIZENOTARIZATION, CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE, pk, dests, nf));
        }

        if (notarizationInputs.size() && GetNotarizationAndFinalization(EVAL_ACCEPTEDNOTARIZATION, mnewTx, dummy, &notarizationIdx, &finalizationIdx))
        {
            // we need to add outputs to pay the reward to the confirmed notary and block miner/staker of that notarization
            // the rest goes back into the notarization thread
            // first input should be the notarization thread
            CTransaction newTx(mnewTx);
            CTransaction confirmedTx;
            CPBaaSNotarization confirmedPBN;
            CBlockIndex *pindex = NULL;
            uint256 hashBlock;
            CBlock confirmedBlock;

            CAmount valueIn;

            BlockMap::iterator it;
            {
                LOCK2(cs_main, mempool.cs);
                // get value in
                CCoinsViewCache view(pcoinsTip);
                int64_t dummyInterest;
                valueIn = view.GetValueIn(chainActive.LastTip()->GetHeight(), &dummyInterest, newTx, chainActive.LastTip()->nTime);

                if (!valueIn)
                {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED, "unable to spend necessary transaction outputs");
                }

                if (confirmedInput != -1)
                {
                    // get data from confirmed tx and block that contains confirmed tx
                    confirmedTx = txesBlkHashes[confirmedIndex].first;
                    hashBlock = txesBlkHashes[confirmedIndex].second;
                    if ((it = mapBlockIndex.find(hashBlock)) != mapBlockIndex.end())
                    {
                        pindex = mapBlockIndex.find(hashBlock)->second;
                    }

                    // add all inputs that might provide notary reward and calculate notary reward based on that plus current
                    // notarization input value divided by number of blocks left in billing period, times blocks per notarization
                    if (pindex)
                    {
                        valueIn += AddNewNotarizationRewards(chainDef, notarizationInputs, mnewTx, pindex->GetHeight());
                    }
                    else
                    {
                        LogPrintf("submitacceptednotarization: cannot find chain %s, possible corrupted database\n", chainDef.name.c_str());
                        printf("submitacceptednotarization: cannot find chain %s, possible corrupted database\n", chainDef.name.c_str());
                    }
                }
            }

            // recipient of notary rewards and miner to share it with
            // notary recipient is the one from the confirmed notarization
            // and miner recipient is from the block it was mined into
            CTxDestination notaryRecipient, minerRecipient;

            // get recipients of any reward output
            if (confirmedInput != -1)
            {
                LOCK(cs_main);
                if (pindex && ReadBlockFromDisk(confirmedBlock, pindex, Params().GetConsensus()) && 
                    (confirmedPBN = CPBaaSNotarization(confirmedTx)).IsValid() &&
                    ExtractDestination(confirmedBlock.vtx[0].vout[0].scriptPubKey, minerRecipient, false))
                {
                    notaryRecipient = CTxDestination(CKeyID(confirmedPBN.notaryKeyID));
                }
                else
                {
                    throw JSONRPCError(RPC_DATABASE_ERROR, "unable to retrieve confirmed notarization data");
                }
            }

            // minimum amount must go to main thread and finalization, then divide what is left among blocks in the billing period
            uint64_t blocksLeft = chainDef.billingPeriod - ((confirmedPBN.notarizationHeight - chainDef.startBlock) % chainDef.billingPeriod);
            CAmount valueOut = 0;
            CAmount notaryValueOut;

            if (valueIn > (CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE << 1))
            {
                if (confirmedInput != -1)
                {
                    if (valueIn < (CPBaaSNotarization::MIN_BLOCKS_BETWEEN_ACCEPTED * (CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE << 1)))
                    {
                        valueOut = valueIn - (CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE << 1);
                    }
                    else
                    {
                        valueOut = (CPBaaSNotarization::MIN_BLOCKS_BETWEEN_ACCEPTED * (valueIn - (CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE << 1))) / blocksLeft;
                    }
                }
                notaryValueOut = valueIn - ((CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE << 1) + valueOut);
            }
            else
            {
                notaryValueOut = 0;
            }

            if (notaryValueOut >= (PBAAS_MINNOTARIZATIONOUTPUT << 1))
            {
                // pay the confirmed notary with
                // notarization reward for this billing period / remaining blocks in the billing period * min blocks in notarization
                // the finalization out has minimum, the notarization out has all the remainder
                // outputs we should have here:
                // 1) notarization out
                // 2) finalization out
                // 3) op_ret
                //
                // send:
                // 66% of output to notary address
                // 33% of output to primary address of block reward
                auto insertIt = mnewTx.vout.begin() + (finalizationIdx + 1);
                if (valueOut)
                {
                    CAmount minerOutput = valueOut / 3;
                    CAmount notaryOutput = valueOut / 3 * 2;
                    mnewTx.vout.insert(insertIt, CTxOut(minerOutput, GetScriptForDestination(minerRecipient)));
                    mnewTx.vout.insert(insertIt, CTxOut(notaryOutput, GetScriptForDestination(notaryRecipient)));
                }
            }
            else
            {
                notaryValueOut += valueOut;
                valueOut = 0;
            }
            
            if ((notaryValueOut + valueOut + CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE) > valueIn)
            {
                notaryValueOut = valueIn;
                printf("Not enough funds to notarize %s\n", chainDef.name.c_str());
                LogPrintf("Not enough funds to notarize %s\n", chainDef.name.c_str());
            }

            CCcontract_info CC;
            CCcontract_info *cp;

            // make the output for the other chain's notarization
            cp = CCinit(&CC, EVAL_ACCEPTEDNOTARIZATION);

            // use public key of cc
            CPubKey pk(ParseHex(CC.CChexstr));
            CKeyID id = CCrossChainRPCData::GetConditionID(pbn.chainID, EVAL_ACCEPTEDNOTARIZATION);
            std::vector<CTxDestination> dests({id});

            mnewTx.vout[notarizationIdx] = MakeCC1of1Vout(EVAL_ACCEPTEDNOTARIZATION, notaryValueOut, pk, dests, pbn);

            CTransaction ntx(mnewTx);

            uint32_t consensusBranchId = CurrentEpochBranchId(chainActive.LastTip()->GetHeight(), Params().GetConsensus());

            // sign the transaction and submit
            for (int i = 0; i < ntx.vin.size(); i++)
            {
                bool signSuccess;
                SignatureData sigdata;
                CAmount value;
                const CScript *pScriptPubKey;

                // if this is our coinbase input, we won't find it elsewhere
                if (i < notarizationInputs.size())
                {
                    pScriptPubKey = &notarizationInputs[i].scriptPubKey;
                    value = notarizationInputs[i].nValue;

                    signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &ntx, i, value, SIGHASH_ALL), *pScriptPubKey, sigdata, consensusBranchId);

                    if (!signSuccess)
                    {
                        fprintf(stderr,"submitacceptednotarization: failure to sign accepted notarization\n");
                        throw JSONRPCError(RPC_VERIFY_ERROR, "Failed to sign notarizaton for " + chainDef.name);
                    } else {
                        UpdateTransaction(mnewTx, i, sigdata);
                    }
                }
            }

            // add to mempool and submit transaction
            CTransaction tx(mnewTx);

            CValidationState state;
            bool fMissingInputs;
            bool accepted;
            {
                LOCK2(cs_main, mempool.cs);
                accepted = AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs);
            }
            if (!accepted) {
                if (state.GetRejectReason() != "")
                {
                    printf("Cannot enter notarization into mempool for chain %s, %s\n", chainDef.name.c_str(), state.GetRejectReason().c_str());
                }
                if (state.IsInvalid()) {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
                } else {
                    if (fMissingInputs) {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                    }
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
                }
            }
            else
            {
                RelayTransaction(tx);
            }

            return newTx.GetHash().GetHex();
        }
    }
    throw JSONRPCError(RPC_VERIFY_REJECTED, "Failed to get notarizaton data for chainID: " + pbn.chainID.GetHex());
}

UniValue getcrossnotarization(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        throw runtime_error(
            "getcrossnotarization \"chainid\" '[\"notarizationtxid1\", \"notarizationtxid2\", ...]'\n"
            "\nReturns the latest PBaaS notarization transaction found in the list of transaction IDs or nothing if not found\n"

            "\nArguments\n"
            "1. \"chainid\"                     (string, required) the hex-encoded chainid to search for notarizations on\n"
            "2. \"txidlist\"                    (stringarray, optional) list of transaction ids to check in preferred order, first found is returned\n"
            "3. \"accepted\"                    (bool, optional) accepted, not earned notarizations, default: true if on\n"
            "                                                    VRSC or VRSCTEST, false otherwise\n"

            "\nResult:\n"
            "{\n"
            "  \"crosstxid\" : \"xxxx\",        (hexstring) cross-transaction id of the notarization that matches, which is one in the arguments\n"
            "  \"txid\" : \"xxxx\",             (hexstring) transaction id of the notarization that was found\n"
            "  \"rawtx\" : \"hexdata\",         (hexstring) entire matching transaction data, serialized\n"
            "  \"newtx\" : \"hexdata\"          (hexstring) the proposed notarization transaction with an opret and opretproof\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getcrossnotarization", "\"chainid\" '[\"notarizationtxid1\", \"notarizationtxid2\", ...]'")
            + HelpExampleRpc("getcrossnotarization", "\"chainid\" '[\"notarizationtxid1\", \"notarizationtxid2\", ...]'")
        );
    }

    uint160 chainID;
    uint32_t ecode;
    UniValue ret(UniValue::VOBJ);

    if (IsVerusActive())
    {
        ecode = EVAL_ACCEPTEDNOTARIZATION;
    }
    else
    {
        ecode = EVAL_EARNEDNOTARIZATION;
    }

    if (params[0].type() == UniValue::VSTR)
    {
        try
        {
            chainID.SetHex(params[0].get_str());
        }
        catch(const std::exception& e)
        {
        }
    }

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chainid");
    }

    if (params.size() > 2)
    {
        if (!params[2].get_bool())
        {
            ecode = EVAL_EARNEDNOTARIZATION;
        }
    }

    uint32_t crosscode;
    if (ecode == EVAL_ACCEPTEDNOTARIZATION)
    {
        crosscode = EVAL_EARNEDNOTARIZATION;
    }
    else
    {
        crosscode = EVAL_ACCEPTEDNOTARIZATION;
    }

    if (params[1].type() != UniValue::VARR)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid second parameter object type: " + itostr(params[1].type()));
    }

    vector<UniValue> values = params[1].getValues();
    set<uint256> txids;
    for (int32_t i = 0; i < values.size(); i++)
    {
        auto txid = uint256S(values[i].get_str());
        if (txid.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter for notarization ID: " + values[i].get_str());
        }
        txids.insert(txid);
    }

    CChainNotarizationData nData;

    LOCK(cs_main);

    vector<pair<CTransaction, uint256>> nTxes;

    // get notarization data and check all transactions
    if (GetNotarizationData(chainID, ecode, nData, &nTxes))
    {
        CTransaction tx;
        CPBaaSNotarization ourLast;
        uint256 blkHash;
        bool found = false;

        // loop in reverse through list, as most recent is at end
        for (int32_t i = nData.vtx.size() - 1; i >= 0; i--)
        {
            const pair<uint256, CPBaaSNotarization> &nzp = nData.vtx[i];
            tx = nTxes[i].first;
            blkHash = nTxes[i].second;
            auto nit = txids.find(nzp.second.crossNotarization);
            if (i == 0 || !(nit == txids.end()))
            {
                found = true;
                // we have the first matching transaction, return it
                ret.push_back(Pair("crosstxid", nzp.second.crossNotarization.GetHex()));
                ret.push_back(Pair("txid", nzp.first.GetHex()));
                ret.push_back(Pair("rawtx", EncodeHexTx(tx)));
                break;
            }
        }

        // now make the basic notarization for this chain that the other chain daemon can complete
        // after it is returned
        if (found)
        {
            // make sure our MMR matches our tip height, etc.
            LOCK(cs_main);

            CPBaaSNotarization prevNotarization(tx);

            if(!prevNotarization.IsValid())
            {
                throw JSONRPCError(RPC_TRANSACTION_ERROR, "Invalid prior notarization");
            }

            int32_t proofheight = chainActive.Height();
            ChainMerkleMountainView mmv(chainActive.GetMMR(), proofheight);
            uint256 mmrRoot = mmv.GetRoot();
            uint256 preHash = mmv.mmr.GetNode(proofheight).hash;

            CMerkleBranch blockProof;
            chainActive.GetBlockProof(mmv, blockProof, proofheight);

            // prove the last notarization txid with new MMR, which also provides its blockhash and power as part of proof
            CBlock block;
            CBlockIndex *pnindex = mapBlockIndex.find(blkHash)->second;

            if(!pnindex || !ReadBlockFromDisk(block, pnindex, Params().GetConsensus(), 0))
            {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
            }

            int32_t prevHeight = pnindex->GetHeight();

            // which transaction are we in this block?
            CKeyID keyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_FINALIZENOTARIZATION));
            std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

            if (!GetAddressIndex(keyID, 1, addressIndex, prevHeight, prevHeight))
            {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Address index read error - possible corruption in address index");
            }

            uint256 txHash = tx.GetHash();
            unsigned int txIndex;
            for (txIndex = 0; txIndex < addressIndex.size(); txIndex++)
            {
                if (addressIndex[txIndex].first.txhash == txHash)
                {
                    break;
                }
            }

            if (txIndex == addressIndex.size())
            {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Notarization not found in address index - possible corruption");
            }
            else
            {
                // get index in the block as our transaction index for proofs
                txIndex = addressIndex[txIndex].first.index;
            }

            // if bock headers are merge mined, keep header refs, not headers

            // create and store the notarization proof of chain
            vector<CBaseChainObject *> chainObjects;
            COpRetProof orp;

            // first, provide the latest block header in the opret...
            CBlockHeader bh = chainActive[proofheight]->GetBlockHeader();
            CChainObject<CBlockHeader> latestHeaderObj(CHAINOBJ_HEADER, bh);
            chainObjects.push_back(&latestHeaderObj);
            orp.AddObject(CHAINOBJ_HEADER, chainActive[proofheight]->GetBlockHash());

            // prove it with the latest MMR root
            CChainObject<CMerkleBranch> latestHeaderProof(CHAINOBJ_PROOF, blockProof);
            chainObjects.push_back(&latestHeaderProof);
            orp.AddObject(bh, chainActive[proofheight]->GetBlockHash());

            // include the last notarization tx, minus its opret in the new notarization's opret
            CMutableTransaction mtx(tx);
            if (mtx.vout[mtx.vout.size() - 1].scriptPubKey.IsOpReturn())
            {
                mtx.vout.pop_back();
            }
            CTransaction strippedTx(mtx);

            // get a proof of the prior notarizaton from the MMR root of this notarization
            CMerkleBranch txProof(txIndex, block.GetMerkleBranch(txIndex));
            chainActive.GetMerkleProof(mmv, txProof, prevHeight);

            // add the cross transaction from this chain to return
            CChainObject<CTransaction> strippedTxObj(CHAINOBJ_TRANSACTION, strippedTx);
            chainObjects.push_back(&strippedTxObj);
            orp.AddObject(CHAINOBJ_TRANSACTION, tx.GetHash());

            // add proof of the transaction
            CChainObject<CMerkleBranch> txProofObj(CHAINOBJ_PROOF, txProof);
            chainObjects.push_back(&txProofObj);
            orp.AddObject(CHAINOBJ_PROOF, txHash);

            // add the MMR block nodes between the last notarization and this one, containing root that combines merkle, block, and compact power hashes
            CPriorBlocksCommitment priorBlocks;
            int numPriorBlocks = proofheight - ourLast.crossHeight;

            if (numPriorBlocks > PBAAS_MAXPRIORBLOCKS || numPriorBlocks > (proofheight - 1))
                numPriorBlocks = PBAAS_MAXPRIORBLOCKS > (proofheight - 1) ? ((proofheight - 1) < 1 ? 0 : (proofheight - 1)) : PBAAS_MAXPRIORBLOCKS;

            // push back the merkle, block hash, and block power commitments for prior blocks to ensure no
            // unintended notary overlap
            for (int i = numPriorBlocks; i >= 0; i--)
            {
                priorBlocks.priorBlocks.push_back(mmv.mmr.GetNode(proofheight - i).hash);
            }

            CChainObject<CPriorBlocksCommitment> priorBlocksObj(CHAINOBJ_PRIORBLOCKS, priorBlocks);
            chainObjects.push_back(&priorBlocksObj);
            orp.AddObject(CHAINOBJ_PRIORBLOCKS, ::GetHash(priorBlocks));

            // get node keys and addresses
            vector<CNodeData> nodes;
            const static int MAX_NODES = 2;

            {
                LOCK(cs_vNodes);
                if (!vNodes.empty())
                {
                    for (int i = 0; i < vNodes.size(); i++)
                    {
                        CNodeStats stats;
                        vNodes[i]->copyStats(stats);
                        if (vNodes[i]->fSuccessfullyConnected && !vNodes[i]->fInbound)
                        {
                            CBitcoinAddress bca(CKeyID(vNodes[i]->hashPaymentAddress));
                            nodes.push_back(CNodeData(vNodes[i]->addr.ToString(), bca.ToString()));
                        }
                    }
                }
            }

            // reduce number to max by removing randomly
            while (nodes.size() > MAX_NODES)
            {
                int toErase = GetRandInt(nodes.size() - 1);
                nodes.erase(nodes.begin() + toErase);
            }

            CKeyID pkID;
            if (USE_EXTERNAL_PUBKEY)
            {
                CPubKey pubKey = CPubKey(ParseHex(NOTARY_PUBKEY));
                if (pubKey.IsFullyValid())
                {
                    pkID = pubKey.GetID();
                }
            }
            else
            {
                static bool printMsg = true;
                if (printMsg)
                {
                    printf("No notary public key recipient has been set, so this node cannot receive rewards for notarization\n");
                    LogPrintf("No notary public key recipient has been set, so this node cannot receive rewards for notarization\n");
                    printMsg = false;
                }
            }

            CBlockIndex *nzIndex = chainActive[proofheight];
            CCurrencyState currencyState = ConnectedChains.GetCurrencyState(proofheight);

            // TODO: get currency state as of proofHeight
            if (ConnectedChains.ThisChain().ChainOptions() & CPBaaSChainDefinition::OPTION_RESERVE)
            {
                CBlock block;
                if (!ReadBlockFromDisk(block, nzIndex, Params().GetConsensus(), false))
                {
                    printf("Cannot read block from disk at height %d\n", nzIndex->GetHeight());
                    LogPrintf("Cannot read block from disk at height %d\n", nzIndex->GetHeight());
                }
            }
            else
            {
                if (proofheight != 1)
                {
                    // get 
                }
            }

            // get the current block's MMR root and proof height
            CPBaaSNotarization notarization = CPBaaSNotarization(CPBaaSNotarization::CURRENT_VERSION, 
                                                                 ASSETCHAINS_CHAINID,
                                                                 pkID,
                                                                 proofheight,
                                                                 mmrRoot,
                                                                 preHash,
                                                                 ArithToUint256(GetCompactPower(nzIndex->nNonce, nzIndex->nBits, nzIndex->nVersion)),
                                                                 currencyState,
                                                                 uint256(), 0,
                                                                 tx.GetHash(), prevNotarization.notarizationHeight,
                                                                 orp,
                                                                 nodes);

            // we now have the chain objects, all associated proofs, and notarization data, make an appropriate transaction template with opret
            // and return it. notarization will need to be completed, so the only thing we really need to construct on this chain is the opret
            CMutableTransaction newNotarization = CreateNewContextualCMutableTransaction(Params().GetConsensus(), proofheight);

            CCcontract_info CC;
            CCcontract_info *cp;

            // make the output for the other chain's notarization
            cp = CCinit(&CC, crosscode);
            // use public key of cc
            CPubKey pk(ParseHex(CC.CChexstr));
            CKeyID id = CCrossChainRPCData::GetConditionID(chainID, crosscode);
            std::vector<CTxDestination> dests({id});

            newNotarization.vout.push_back(MakeCC1of1Vout(crosscode, CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE, pk, dests, notarization));

            // make the unspent finalization output
            cp = CCinit(&CC, EVAL_FINALIZENOTARIZATION);
            pk = CPubKey(ParseHex(CC.CChexstr));
            dests = std::vector<CTxDestination>({CKeyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_FINALIZENOTARIZATION))});

            CNotarizationFinalization nf;
            newNotarization.vout.push_back(MakeCC1of1Vout(EVAL_FINALIZENOTARIZATION, DEFAULT_TRANSACTION_FEE, pk, dests, nf));

            newNotarization.vout.push_back(CTxOut(0, StoreOpRetArray(chainObjects)));

            CTransaction newTx(newNotarization);
            ret.push_back(Pair("newtx", EncodeHexTx(newTx)));
        }
    }
    return ret;
}

UniValue paynotarizationrewards(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 2 && params.size() != 3))
    {
        throw runtime_error(
            "paynotarizationrewards \"chainid\" \"amount\" \"billingperiod\"\n"
            "\nAdds some amount of funds to a specific billing period of a PBaaS chain, which will be released\n"
            "\nin the form of payments to notaries whose notarizations are confirmed.\n"

            "\nArguments\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("paynotarizationrewards", "\"hextx\"")
            + HelpExampleRpc("paynotarizationrewards", "\"hextx\"")
        );
    }
    CheckPBaaSAPIsValid();

    uint160 chainID;

    if (!pwalletMain)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Paying notarization rewards requires an active wallet");
    }

    chainID = GetChainIDFromParam(params[0]);
    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid PBaaS name or chainid");
    }

    CAmount amount = AmountFromValue(params[1]);
    uint32_t billingPeriod = 0;

    if (params.size() == 3)
    {
        uint32_t billingPeriod = uni_get_int(params[2]);
    }

    CServiceReward sr = CServiceReward(SERVICE_NOTARIZATION, billingPeriod);
    
    CCcontract_info CC;
    CCcontract_info *cp;
    cp = CCinit(&CC, EVAL_SERVICEREWARD);

    CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

    std::vector<CTxDestination> dests({CKeyID(CCrossChainRPCData::GetConditionID(CCrossChainRPCData::GetConditionID(chainID, SERVICE_NOTARIZATION), EVAL_SERVICEREWARD))});

    std::vector<CRecipient> outputs = std::vector<CRecipient>({{MakeCC1of1Vout(EVAL_SERVICEREWARD, amount, pk, dests, sr).scriptPubKey, amount}});
    CWalletTx wtx;

    // create the transaction with native coin as input
    LOCK2(cs_main, pwalletMain->cs_wallet);

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainID.GetHex() + ": " + failReason);
    }
    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }
    return UniValue(wtx.GetHash().GetHex());
}

UniValue listreservetransactions(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "listreservetransactions (maxnumber) (minconfirmations)\n"
            "\nLists all reserve coin transactions sent to/from the current wallet.\n"

            "\nArguments\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("listreservetransactions", "100 0")
            + HelpExampleRpc("listreservetransactions", "100 0")
        );
    }
    // lists all transactions in a wallet that are 
    return NullUniValue;
}

CCoinbaseCurrencyState GetInitialCurrencyState(CPBaaSChainDefinition &chainDef, int32_t definitionHeight)
{
    std::multimap<uint160, pair<CInputDescriptor, CReserveTransfer>> transferInputs;
    CAmount preconvertedAmount = 0, fees = 0;
    bool isReserve = chainDef.ChainOptions() & CPBaaSChainDefinition::OPTION_RESERVE;

    if (GetChainTransfers(transferInputs, chainDef.GetChainID(), definitionHeight, chainDef.startBlock, CReserveTransfer::PRECONVERT | CReserveTransfer::VALID))
    {
        for (auto transfer : transferInputs)
        {
            // total amount will be transferred to the chain, with fee split between aggregator and miner in
            // Verus reserve
            preconvertedAmount += transfer.second.second.nValue;
            fees += transfer.second.second.nFees;
        }
    }

    uint32_t Flags = isReserve ? CCurrencyState::VALID + CCurrencyState::ISRESERVE : CCurrencyState::VALID;
    CCurrencyState currencyState(chainDef.conversion, 0, 0, 0, preconvertedAmount, Flags);

    CAmount preconvertedNative = currencyState.ReserveToNative(preconvertedAmount, chainDef.conversion);
    currencyState.InitialSupply = preconvertedNative;
    currencyState.Supply += preconvertedNative;

    return CCoinbaseCurrencyState(currencyState, preconvertedAmount, 0, CReserveOutput(CReserveOutput::VALID, fees), chainDef.conversion, fees, fees);
}

UniValue reserveexchange(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "reserveexchange '[{\"toreserve\": 1, \"recipient\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0}]'\n"
            "\nThis sends a Verus output as a JSON object or lists of Verus outputs as a list of objects to an address on the same or another chain.\n"
            "\nFunds are sourced automatically from the current wallet, which must be present, as in sendtoaddress.\n"

            "\nArguments\n"
            "       {\n"
            "           \"toreserve\"      : \"bool\",  (bool,   optional) if present, conversion is to the underlying reserve (Verus), if false, from Verus\n"
            "           \"recipient\"      : \"Rxxx\",  (string, required) recipient of converted funds or funds that failed to convert\n"
            "           \"amount\"         : \"n\",     (int64,  required) amount of source coins that will be converted, depending on the toreserve flag, the rest is change\n"
            "           \"limit\"          : \"n\",     (int64,  optional) price in reserve limit, below which for buys and above which for sells, execution will occur\n"
            "           \"validbefore\"    : \"n\",     (int,    optional) block before which this can execute as a conversion, otherwise, it executes as a send with normal network fee\n"
            "           \"subtractfee\"    : \"bool\",  (bool,   optional) if true, reduce amount to destination by the fee amount, otherwise, add from inputs to cover fee"
            "       }\n"

            "\nResult:\n"
            "       \"txid\" : \"transactionid\" (string) The transaction id.\n"

            "\nExamples:\n"
            + HelpExampleCli("reserveexchange", "'[{\"name\": \"PBAASCHAIN\", \"paymentaddress\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0}]'")
            + HelpExampleRpc("reserveexchange", "'[{\"name\": \"PBAASCHAIN\", \"paymentaddress\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0}]'")
        );
    }

    CheckPBaaSAPIsValid();
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Not yet implemented. Use sendreserve in this release.");
}

UniValue sendreserve(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "sendreserve '{\"name\": \"PBAASCHAIN\", \"paymentaddress\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0, \"convert\": 1}' (returntx)\n"
            "\nThis sends a Verus output as a JSON object or lists of Verus outputs as a list of objects to an address on the same or another chain.\n"
            "\nFunds are sourced automatically from the current wallet, which must be present, as in sendtoaddress.\n"

            "\nArguments\n"
            "       {\n"
            "           \"name\"           : \"xxxx\",  (string, optional) Verus ecosystem-wide name/symbol of chain to send to, if absent, current chain is assumed\n"
            "           \"paymentaddress\" : \"Rxxx\",  (string, required) transaction recipient address\n"
            "           \"refundaddress\"  : \"Rxxx\",  (string, optional) if a pre-convert is not mined in time, funds can be spent by the owner of this address\n"
            "           \"amount\"         : \"n.n\",   (value,  required) coins that will be moved and sent to address on PBaaS chain, network and conversion fees additional\n"
            "           \"tonative\"       : \"false\", (bool,   optional) auto-convert from Verus to PBaaS currency at market price\n"
            "           \"toreserve\"      : \"false\", (bool,   optional) auto-convert from PBaaS to Verus reserve currency at market price\n"
            "           \"preconvert\"     : \"false\", (bool,   optional) auto-convert to PBaaS currency at market price, this only works if the order is mined before block start of the chain\n"
            "           \"subtractfee\"    : \"bool\",  (bool,   optional) if true, reduce amount to destination by the transfer and conversion fee amount. normal network fees are never subtracted"
            "       }\n"
            "       \"returntx\"                        (bool,   optional) defaults to false and transaction is sent, if true, transaction is signed by this wallet and returned\n"

            "\nResult:\n"
            "       \"txid\" : \"transactionid\" (string) The transaction id.\n"

            "\nExamples:\n"
            + HelpExampleCli("sendreserve", "'{\"name\": \"PBAASCHAIN\", \"paymentaddress\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0}'")
            + HelpExampleRpc("sendreserve", "'{\"name\": \"PBAASCHAIN\", \"paymentaddress\": \"RRehdmUV7oEAqoZnzEGBH34XysnWaBatct\", \"amount\": 5.0}'")
        );
    }

    CheckPBaaSAPIsValid();

    // each object represents a send, and all sends are aggregated into one transaction to improve potential for scaling when moving funds between
    // and across multiple chains.
    //
    // each output will require an additional standard cross-chain fee that will be divided evenly in two ways,
    // between the transaction aggregator -- the miner or staker who creates the aggregating export, 
    // and the transaction importer on the alternate chain who posts each exported bundle.
    //
    vector<CRecipient> outputs;
    vector<bool> vConvert;

    if ((!params[0].isObject()))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters. Must provide a single object that represents valid outputs. see help.");
    }

    bool isVerusActive = IsVerusActive();
    uint160 thisChainID = ConnectedChains.ThisChain().GetChainID();

    CWalletTx wtx;

    // default double fee for miner of chain definition tx
    // one output for definition, one for finalization
    string name = uni_get_str(find_value(params[0], "name"), "");
    string paymentAddr = uni_get_str(find_value(params[0], "paymentaddress"), "");
    string refundAddr = uni_get_str(find_value(params[0], "refundaddress"), paymentAddr);
    CAmount amount = AmountFromValue(find_value(params[0], "amount"));

    bool tonative = uni_get_int(find_value(params[0], "tonative"), false);
    bool toreserve = uni_get_int(find_value(params[0], "toreserve"), false);
    bool preconvert = uni_get_int(find_value(params[0], "preconvert"), false);

    bool subtractFee = uni_get_int(find_value(params[0], "subtractfee"), false);
    uint32_t flags = CReserveOutput::VALID;

    bool sameChain = false;
    if (name == "")
    {
        name = ASSETCHAINS_SYMBOL;
        sameChain = true;
    }

    if (name == "" || paymentAddr == "" || amount < 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters");
    }

    CBitcoinAddress ba(DecodeDestination(paymentAddr));
    CKeyID kID;

    if (!ba.IsValid() || !ba.GetKeyID(kID))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid payment address");
    }

    CTxDestination refundDest = DecodeDestination(refundAddr);
    CBitcoinAddress refunda(refundDest);
    CKeyID refundID;

    if (!refunda.IsValid() || !refunda.GetKeyID(refundID))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid refund address");
    }

    uint160 chainID = CCrossChainRPCData::GetChainID(name);

    CPBaaSChainDefinition chainDef;
    int32_t definitionHeight;

    // validate that the target chain is still running
    if (!GetChainDefinition(chainID, chainDef, &definitionHeight) || !chainDef.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain specified is not a valid chain");
    }

    bool isReserve = chainDef.ChainOptions() & CPBaaSChainDefinition::OPTION_RESERVE;

    // non-reserve conversions only work before the chain launches, then they determine the premine allocation
    // premine claim transactions are basically export transactions of a pre-converted amount of native coin with a neutral
    // impact on the already accounted for supply based on conversion conditions and contributions to the blockchain before
    // the start block
    int32_t height = chainActive.Height();
    bool beforeStart = chainDef.startBlock > height;
    CCoinbaseCurrencyState currencyState;

    UniValue ret = NullUniValue;

    if (isVerusActive)
    {
        if (chainID == thisChainID)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot send reserve or convert, except on a PBaaS fractional reserve chain. Use sendtoaddress or z_sendmany.");
        }
        else // ensure the PBaaS chain is a fractional reserve or that it's convertible and this is a conversion
        {
            if (tonative || preconvert)
            {
                // if chain hasn't started yet, we use the conversion as a ratio over satoshis to participate in the pre-mine
                // up to a maximum
                if (beforeStart)
                {
                    if (chainDef.conversion <= 0)
                    {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(ASSETCHAINS_SYMBOL) + " is not convertible to " + chainDef.name + ".");
                    }

                    currencyState = GetInitialCurrencyState(chainDef, definitionHeight);

                    if (tonative)
                    {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Until " + chainDef.name + " launches, you must use \"preconvert\" rather than \"tonative\" for conversion.");
                    }

                    flags |= CReserveTransfer::PRECONVERT;
                } else if (!isReserve || preconvert)
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot preconvert " + std::string(ASSETCHAINS_SYMBOL) + " after chain launch");
                }
                else
                {
                    flags |= CReserveTransfer::CONVERT;
                }
            }
            else if (!isReserve)
            {
                // only reserve currencies can have reserve sent to them
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(ASSETCHAINS_SYMBOL) + " is not a reserve for " + chainDef.name + " and cannot be sent to its chain.");
            }
        }

        CCcontract_info CC;
        CCcontract_info *cp;
        cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);

        CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

        // send the entire amount to a reserve transfer output bound to this chain
        std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(ConnectedChains.ThisChain().GetConditionID(EVAL_RESERVE_TRANSFER)), CKeyID(chainID)});

        // start with default fee for this send
        CAmount transferFee = CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;

        if (flags & CReserveTransfer::PRECONVERT)
        {
            arith_uint256 bigAmount(amount);
            static const arith_uint256 bigsatoshi(CReserveExchange::SATOSHIDEN);
            arith_uint256 feeRate(chainDef.launchFee);

            if (chainDef.launchFee)
            {
                CAmount launchFee;

                if (!subtractFee)
                {
                    amount = (bigAmount = ((bigAmount * bigsatoshi) / (bigsatoshi - feeRate))).GetLow64();
                }
                launchFee = ((bigAmount * feeRate) / bigsatoshi).GetLow64();

                amount -= launchFee;

                // add the output to this transaction
                CTxDestination feeOutAddr(chainDef.address);
                outputs.push_back(CRecipient({GetScriptForDestination(feeOutAddr), launchFee, false}));
            }

            if (!subtractFee)
            {
                // add the fee amount that will make the conversion correct after subtracting it again
                amount += CReserveTransactionDescriptor::CalculateAdditionalConversionFee(amount);
            }

            if (currencyState.ReserveIn + amount > chainDef.maxpreconvert)
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(ASSETCHAINS_SYMBOL) + " contribution maximum does not allow this conversion. Maximum participation remaining is " + ValueFromAmount(chainDef.maxpreconvert - currencyState.ReserveIn).write());
            }
        }
        else if (flags & CReserveTransfer::CONVERT && !subtractFee)
        {
            // add the fee amount that will make the conversion correct after subtracting it again
            amount += CReserveTransactionDescriptor::CalculateAdditionalConversionFee(amount);
        }

        if (!subtractFee)
        {
            amount += transferFee;
        }

        if (amount <= (transferFee << 1))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must send more than twice the cost of the fee. Fee for this transaction is " + ValueFromAmount(transferFee).write());
        }

        transferFee = CReserveTransfer::CalculateFee(flags, amount, chainDef);
        amount -= transferFee;

        // create the transfer object
        CReserveTransfer rt(flags, amount, transferFee, kID);

        CTxOut ccOut;

        // if preconversion and we have a minpreconvert, prepare for refund output
        if (flags & CReserveTransfer::PRECONVERT && chainDef.minpreconvert)
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            CPubKey pk2(GetDestinationBytes(refundDest));
            if (!pk2.IsFullyValid() && !pwalletMain->GetPubKey(refundID, pk2))
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot retrieve public key for refund address. Refund address must either be public key or address that is accessible from the current wallet.");
            }
            dests.push_back(pk2);
            ccOut = MakeCC1of2Vout(EVAL_RESERVE_TRANSFER, amount + transferFee, pk, pk2, dests, (CReserveTransfer)rt);
        }
        else
        {
            // cast object to most derived class to ensure template works on all compilers
            ccOut = MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, amount + transferFee, pk, dests, (CReserveTransfer)rt);
        }

        outputs.push_back(CRecipient({ccOut.scriptPubKey, amount + transferFee, false}));

        // create the transaction with native coin as input
        LOCK2(cs_main, pwalletMain->cs_wallet);

        CReserveKey reserveKey(pwalletMain);
        CAmount fee;
        int nChangePos;
        string failReason;

        if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
        {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
        }
        if (!pwalletMain->CommitTransaction(wtx, reserveKey))
        {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
        }
        ret = UniValue(wtx.GetHash().GetHex());
    }
    else
    {
        if (chainID == thisChainID)
        {
            CCcontract_info CC;
            CCcontract_info *cp;

            // if we are on a fractional reserve chain, a conversion will convert FROM the fractional reserve currency TO
            // the reserve. this will basically turn into a reserveexchange transaction
            if (toreserve)
            {
                if (!isReserve)
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot convert " + ConnectedChains.ThisChain().name + " into " + ConnectedChains.NotaryChain().chainDefinition.name + ".");
                }

                CCoinsViewCache view(pcoinsTip);

                // we will send using a reserve output, fee will be paid by converting from reserve
                cp = CCinit(&CC, EVAL_RESERVE_EXCHANGE);
                CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

                std::vector<CTxDestination> dests = std::vector<CTxDestination>({kID, CTxDestination(pk)});

                if (!subtractFee)
                {
                    amount += CReserveTransactionDescriptor::CalculateAdditionalConversionFee(amount);
                }

                CReserveExchange rex(flags + CReserveExchange::TO_RESERVE, amount);

                CTxOut ccOut = MakeCC1ofAnyVout(EVAL_RESERVE_EXCHANGE, amount, dests, rex, pk);
                outputs.push_back(CRecipient({ccOut.scriptPubKey, amount, false}));

                // create a transaction with native coin as input
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);

                    CReserveKey reserveKey(pwalletMain);
                    CAmount fee;
                    int nChangePos;
                    string failReason;

                    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
                    }
                    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
                    }
                    ret = UniValue(wtx.GetHash().GetHex());
                }
            }
            else if (tonative)
            {
                // convert from reserve to native (buy)
                cp = CCinit(&CC, EVAL_RESERVE_EXCHANGE);
                CPubKey pk(ParseHex(CC.CChexstr));

                std::vector<CTxDestination> dests = std::vector<CTxDestination>({CTxDestination(kID), CTxDestination(pk)});

                if (!subtractFee)
                {
                    amount += CReserveTransactionDescriptor::CalculateAdditionalConversionFee(amount);
                }

                CReserveExchange rex = CReserveExchange(CReserveExchange::VALID, amount);

                CTxOut ccOut = MakeCC1ofAnyVout(EVAL_RESERVE_EXCHANGE, 0, dests, rex, pk);
                outputs.push_back(CRecipient({ccOut.scriptPubKey, 0, false}));

                // create a transaction with reserve coin as input
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);

                    CReserveKey reserveKey(pwalletMain);
                    CAmount fee;
                    int nChangePos;
                    string failReason;

                    if (!pwalletMain->CreateReserveTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
                    }
                    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit reserve transaction " + wtx.GetHash().GetHex());
                    }
                    ret = UniValue(wtx.GetHash().GetHex());
                }
            }
            else
            {
                // we will send using a reserve output, fee will be paid by converting from reserve
                cp = CCinit(&CC, EVAL_RESERVE_OUTPUT);

                std::vector<CTxDestination> dests = std::vector<CTxDestination>({kID});

                // create the transfer object
                CReserveOutput ro(flags, amount);

                // native amount in the output is 0
                CTxOut ccOut = MakeCC1ofAnyVout(EVAL_RESERVE_OUTPUT, 0, dests, ro);

                outputs.push_back(CRecipient({ccOut.scriptPubKey, amount, subtractFee}));

                // create a transaction with reserve coin as input
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);

                    CReserveKey reserveKey(pwalletMain);
                    CAmount fee;
                    int nChangePos;
                    string failReason;

                    if (!pwalletMain->CreateReserveTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
                    }
                    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
                    }
                    ret = UniValue(wtx.GetHash().GetHex());
                }
            }
        }
        else if (chainID == ConnectedChains.NotaryChain().GetChainID())
        {
            // send Verus from this PBaaS chain to the Verus chain, if converted, the inputs will be the PBaaS native coin, and we will convert to
            // Verus for the send
            if (tonative)
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot convert to native and send. Can only send " + chainDef.name + " reserve to the chain.");
            }

            if (toreserve)
            {
                if (!isReserve)
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot convert " + ConnectedChains.ThisChain().name + " into " + ConnectedChains.NotaryChain().chainDefinition.name + ".");
                }

                CAmount conversionFee;
                if (subtractFee)
                {
                    conversionFee = CReserveTransactionDescriptor::CalculateConversionFee(amount);
                    amount -= conversionFee;
                }
                else
                {
                    conversionFee = CReserveTransactionDescriptor::CalculateAdditionalConversionFee(amount);
                }

                CCoinsViewCache view(pcoinsTip);
                CReserveExchange rex(flags + CReserveExchange::TO_RESERVE + CReserveExchange::SEND_OUTPUT, amount + conversionFee);

                // we will send using a reserve output, fee will be paid by converting from reserve
                CCcontract_info CC;
                CCcontract_info *cp;
                cp = CCinit(&CC, EVAL_RESERVE_EXCHANGE);
                CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

                std::vector<CTxDestination> dests = std::vector<CTxDestination>({kID});

                // native amount in output is 0
                CTxOut ccOut = MakeCC1ofAnyVout(EVAL_RESERVE_EXCHANGE, amount + conversionFee, dests, rex, pk);

                outputs.push_back(CRecipient({ccOut.scriptPubKey, amount + conversionFee, false}));

                // create a transaction with native coin as input
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);

                    CReserveKey reserveKey(pwalletMain);
                    CAmount fee;
                    int nChangePos;
                    string failReason;

                    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
                    }
                    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
                    }
                    ret = UniValue(wtx.GetHash().GetHex());
                }
            }
            else
            {
                // we will send using from reserve input without conversion
                // output will be a reserve transfer
                CCcontract_info CC;
                CCcontract_info *cp;
                cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);

                CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

                // send the entire amount to a reserve transfer output of the specific chain
                std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(ConnectedChains.ThisChain().GetConditionID(EVAL_RESERVE_TRANSFER)), CKeyID(chainID)});

                CAmount transferFee = CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;
                if (subtractFee)
                {
                    amount -= transferFee;
                }

                // create the transfer object
                CReserveTransfer rt(flags, amount, CReserveTransfer::DEFAULT_PER_STEP_FEE << 1, kID);

                CTxOut ccOut = MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, 0, pk, dests, (CReserveTransfer)rt);
                outputs.push_back(CRecipient({ccOut.scriptPubKey, 0, subtractFee}));

                // create a transaction with reserve coin as input
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);

                    CReserveKey reserveKey(pwalletMain);
                    CAmount fee;
                    int nChangePos;
                    string failReason;

                    if (!pwalletMain->CreateReserveTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, chainDef.name + ": " + failReason);
                    }
                    /*
                    printf("newTx outputs:\n");
                    for (auto outp : wtx.vout)
                    {
                        UniValue scrOut(UniValue::VOBJ);
                        ScriptPubKeyToJSON(outp.scriptPubKey, scrOut, false);
                        printf("%s\n", scrOut.write(1, 2).c_str());
                    }
                    */
                    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
                    {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
                    }
                    ret = UniValue(wtx.GetHash().GetHex());
                }
            }
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain specified is not the current chain or this chain's reserve");
        }
    }
    return ret;
}

bool GetLastImportIn(uint160 chainID, CTransaction &lastImportTx)
{
    // look for unspent chain transfer outputs for all chains
    CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_IMPORT);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    LOCK(cs_main);

    if (!GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        return false;
    }

    for (auto output : unspentOutputs)
    {
        // find the first one that is either in block 1, part of a chain definition transaction, or spends a prior
        // import output in input 0, which cannot be done unless the output is rooted in a valid import thread. anyone can try to send
        // coins to this address without being part of the chain, but they will be wasted and unspendable.
        COptCCParams p;
        if (output.second.script.IsPayToCryptoCondition(p) && p.IsValid() && p.evalCode == EVAL_CROSSCHAIN_IMPORT)
        {
            // we actually don't care what is in the transaction here, only that it is a valid cross chain import tx
            // that is either the first in a chain or spends a valid cross chain import output
            CCrossChainImport cci(p.vData[0]);
            CTransaction lastTx;
            uint256 blkHash;
            if (cci.IsValid() && myGetTransaction(output.first.txhash, lastTx, blkHash))
            {
                if (output.second.blockHeight == 1 && lastTx.IsCoinBase())
                {
                    lastImportTx = lastTx;
                    return true;
                }
                else
                {
                    if (IsVerusActive())
                    {
                        // if this is the Verus chain, then we need to either be part of a chain definition transaction
                        // or spend a valid import output
                        if (CPBaaSChainDefinition(lastTx).IsValid())
                        {
                            lastImportTx = lastTx;
                            return true;
                        }
                        // if we get here, we must spend a valid import output
                        CTransaction inputTx;
                        uint256 inputBlkHash;
                        if (lastTx.vin.size() && myGetTransaction(lastTx.vin[0].prevout.hash, inputTx, inputBlkHash) && CCrossChainImport(lastTx).IsValid())
                        {
                            lastImportTx = lastTx;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

UniValue getlastimportin(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getlastimportin \"fromname\"\n"
            "\nThis returns the last import transaction from the chain specified and a blank transaction template to use when making new\n"
            "\nimport transactions. Since the typical use for this call is to make new import transactions from the other chain that will be then\n"
            "\nbroadcast to this chain, we include the template by default.\n"

            "\nArguments\n"
            "   \"fromname\"                (string, required) name of the chain to get the last import transaction in from\n"

            "\nResult:\n"
            "   {\n"
            "       \"lastimporttransaction\": \"hex\"      Hex encoded serialized import transaction\n"
            "       \"lastconfirmednotarization\" : \"hex\" Hex encoded last confirmed notarization transaction\n"
            "       \"importtxtemplate\": \"hex\"           Hex encoded import template for new import transactions\n"
            "       \"totalimportavailable\": \"amount\"    Total amount if native import currency available to import as native\n"
            "   }\n"

            "\nExamples:\n"
            + HelpExampleCli("getlastimportin", "jsondefinition")
            + HelpExampleRpc("getlastimportin", "jsondefinition")
        );
    }
    // starting from that last transaction id, see if we have any newer to export for the indicated chain, and if so, return them as
    // import transactions using the importtxtemplate as a template
    CheckPBaaSAPIsValid();

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CTransaction lastImportTx, lastConfirmedTx;

    CChainNotarizationData cnd;
    std::vector<std::pair<CTransaction, uint256>> txesOut;

    LOCK(cs_main);

    if (!GetNotarizationData(chainID, IsVerusActive() ? EVAL_ACCEPTEDNOTARIZATION : EVAL_EARNEDNOTARIZATION, cnd, &txesOut))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No chain notarizations for " + uni_get_str(params[0]) + " found");
    }

    UniValue ret(UniValue::VNULL);

    if (cnd.IsConfirmed())
    {
        lastConfirmedTx = txesOut[cnd.lastConfirmed].first;
        if (GetLastImportIn(chainID, lastImportTx))
        {
            ret = UniValue(UniValue::VOBJ);
            CMutableTransaction txTemplate = CreateNewContextualCMutableTransaction(Params().GetConsensus(), chainActive.Height());
            // find the import output
            COptCCParams p;
            int importIdx;
            for (importIdx = 0; importIdx < lastImportTx.vout.size(); importIdx++)
            {
                if (lastImportTx.vout[importIdx].scriptPubKey.IsPayToCryptoCondition(p) && p.IsValid() && p.evalCode == EVAL_CROSSCHAIN_IMPORT)
                {
                    break;
                }
            }
            if (importIdx >= lastImportTx.vout.size())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid lastImport transaction");
            }

            txTemplate.vin.push_back(CTxIn(lastImportTx.GetHash(), importIdx, CScript()));

            // if Verus is active, our template import will gather all RESERVE_DEPOSITS that it can to spend into the
            // import thread
            if (IsVerusActive())
            {
                std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > reserveDeposits;

                LOCK2(cs_main, mempool.cs);

                if (GetAddressUnspent(CKeyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_RESERVE_DEPOSIT)), 1, reserveDeposits))
                {
                    for (auto deposit : reserveDeposits)
                    {
                        COptCCParams p;
                        if (deposit.second.script.IsPayToCryptoCondition(p) && p.evalCode == EVAL_RESERVE_DEPOSIT)
                        {
                            txTemplate.vin.push_back(CTxIn(deposit.first.txhash, deposit.first.index, CScript()));
                        }
                    }
                }
            }

            CCoinsViewCache view(pcoinsTip);
            int64_t dummyInterest;
            CAmount totalImportAvailable = view.GetValueIn(cnd.vtx[cnd.lastConfirmed].second.notarizationHeight, &dummyInterest, txTemplate);

            ret.push_back(Pair("lastimporttransaction", EncodeHexTx(lastImportTx)));
            ret.push_back(Pair("lastconfirmednotarization", EncodeHexTx(lastConfirmedTx)));
            ret.push_back(Pair("importtxtemplate", EncodeHexTx(txTemplate)));
            ret.push_back(Pair("totalimportavailable", totalImportAvailable));
        }
    }
    return ret;
}

UniValue getlatestimportsout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getlatestimportsout \"name\" \"lastimporttransaction\" \"importtxtemplate\"\n"
            "\nThis creates and returns all new imports for the specified chain that are exported from this blockchain.\n"

            "\nArguments\n"
            "{\n"
            "   \"name\" : \"string\"                   (string, required) name of the chain to get the export transactions for\n"
            "   \"lastimporttransaction\" : \"hex\"     (hex,    required) the last import transaction to follow, return list starts with import that spends this\n"
            "   \"lastconfirmednotarization\" : \"hex\" (hex,    required) the last confirmed notarization transaction\n"
            "   \"importtxtemplate\" : \"hex\"          (hex,    required) template transaction to use, so we create a transaction compatible with the other chain\n"
            "   \"totalimportavailable\": \"amount\"    (number,  required) Total amount if native import currency available to import as native\n"
            "}\n"

            "\nResult:\n"
            "UniValue array of hex transactions"

            "\nExamples:\n"
            + HelpExampleCli("getlatestimportsout", "jsonargs")
            + HelpExampleRpc("getlatestimportsout", "jsonargs")
        );
    }

    // starting from that last transaction id, see if we have any newer to export for the indicated chain, and if so, return them as
    // import transactions using the importtxtemplate as a template
    CheckPBaaSAPIsValid();

    bool isVerusActive = IsVerusActive();

    uint160 chainID = GetChainIDFromParam(find_value(params[0], "name"));

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    // starting from that last transaction id, see if we have any newer to export for the indicated chain, and if so, return them as
    // import transactions using the importtxtemplate as a template
    CPBaaSChainDefinition chainDef;

    if (!GetChainDefinition(chainID, chainDef))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain definition not found");
    }

    std::string lastImportHex = uni_get_str(find_value(params[0], "lastimporttx"));
    CTransaction lastImportTx;

    std::string templateTxHex = uni_get_str(find_value(params[0], "importtxtemplate"));
    CTransaction templateTx;

    std::string lastConfirmedTxHex = uni_get_str(find_value(params[0], "lastconfirmednotarization"));
    CTransaction lastConfirmedNotarization;

    CAmount totalImportAvailable = uni_get_int64(find_value(params[0], "totalimportavailable"));

    std::vector<CBaseChainObject *> chainObjs;
    if (!(DecodeHexTx(lastImportTx, lastImportHex) && 
          DecodeHexTx(templateTx, templateTxHex) &&
          DecodeHexTx(lastConfirmedNotarization, lastConfirmedTxHex) &&
          CCrossChainImport(lastImportTx).IsValid() &&
          (CPBaaSChainDefinition(lastImportTx).IsValid() ||
          (lastImportTx.vout.back().scriptPubKey.IsOpReturn() &&
          (chainObjs = RetrieveOpRetArray(lastImportTx.vout.back().scriptPubKey)).size() >= 2 &&
          chainObjs[0]->objectType == CHAINOBJ_TRANSACTION &&
          chainObjs[1]->objectType == CHAINOBJ_CROSSCHAINPROOF))))
    {
        DeleteOpRetObjects(chainObjs);
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid last import tx");
    }

    DeleteOpRetObjects(chainObjs);

    std::vector<CTransaction> newImports;
    if (!ConnectedChains.CreateLatestImports(chainDef, lastImportTx, templateTx, lastConfirmedNotarization, totalImportAvailable, newImports))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failure creating new imports");
    }
    UniValue ret(UniValue::VARR);

    for (auto import : newImports)
    {
        ret.push_back(EncodeHexTx(import));
    }
    return ret;
}

// refunds a failed launch if it is eligible and not already refunded. if it fails to refund, it returns false
// and a string with the reason for failure
bool RefundFailedLaunch(uint160 chainID, CTransaction &lastImportTx, std::vector<CTransaction> &newRefunds, std::string &errorReason)
{
    int32_t defHeight = 0;
    CPBaaSChainDefinition chainDef;
    CTransaction chainDefTx;
    uint160 thisChainID = ConnectedChains.ThisChain().GetChainID();

    if (!GetChainDefinition(chainID, chainDef, &defHeight) || chainID == thisChainID)
    {
        errorReason = "chain-not-found-or-ineligible";
        return false;
    }

    bool valid = false;

    LOCK(cs_main);

    // validate refund eligibility and that the chain is even active for refunds
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    if (GetAddressIndex(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION)), 1, addressIndex, defHeight, defHeight) && addressIndex.size())
    //if (GetAddressUnspent(CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION)), 1, unspentOutputs) && unspentOutputs.size())
    {
        uint256 blkHash;

        for (auto txidx : addressIndex)
        {
            COptCCParams p;
            if (!myGetTransaction(txidx.first.txhash, chainDefTx, blkHash))
            {
                errorReason = "chain-transaction-not-found";
                return false;
            }
            if (chainDefTx.vout[txidx.first.index].scriptPubKey.IsPayToCryptoCondition(p) && 
                p.IsValid() && 
                p.evalCode == EVAL_PBAASDEFINITION && 
                p.vData[0].size() && 
                (chainDef = CPBaaSChainDefinition(p.vData[0])).IsValid() &&
                chainDef.GetChainID() == chainID)
            {
                valid = true;
                break;
            }
        }
    }

    if (!valid)
    {
        errorReason = "valid-chain-not-found";
        return false;
    }

    if (!GetLastImportIn(chainID, lastImportTx))
    {
        errorReason = "no-import-thread-found";
        return false;
    }

    std::vector<CBaseChainObject *> chainObjs;
    // either a fully valid import with an export or the first import either in block 1 or the chain definition
    // on the Verus chain
    if (!(lastImportTx.vout.back().scriptPubKey.IsOpReturn() &&
          (chainObjs = RetrieveOpRetArray(lastImportTx.vout.back().scriptPubKey)).size() >= 2 &&
          chainObjs[0]->objectType == CHAINOBJ_TRANSACTION &&
          chainObjs[1]->objectType == CHAINOBJ_CROSSCHAINPROOF) &&
        !(chainObjs.size() == 0 && CPBaaSChainDefinition(lastImportTx).IsValid()))
    {
        DeleteOpRetObjects(chainObjs);
        errorReason = "invalid-import-thread-found";
        return false;
    }

    bool isVerusActive = IsVerusActive();

    uint256 lastExportHash;
    CTransaction lastExportTx;

    uint32_t blkHeight = defHeight;
    bool found = false;
    if (chainObjs.size())
    {
        CTransaction tx;
        uint256 blkHash;
        lastExportTx = ((CChainObject<CTransaction> *)chainObjs[0])->object;
        lastExportHash = lastExportTx.GetHash();
        BlockMap::iterator blkMapIt;
        DeleteOpRetObjects(chainObjs);
        if (myGetTransaction(lastExportHash, tx, blkHash) &&
            (blkMapIt = mapBlockIndex.find(blkHash)) != mapBlockIndex.end() && 
            blkMapIt->second)
        {
            found = true;
            blkHeight = blkMapIt->second->GetHeight();
        }
    }
    else
    {
        // if we haven't processed any refunds yet, setup to payout from the first export and continue
        CCrossChainExport ccx(lastImportTx);
        if (ccx.IsValid())
        {
            found = true;
            lastExportTx = lastImportTx;
            lastExportHash = lastImportTx.GetHash();
        }
    }

    if (!found)
    {
        LogPrintf("%s: No export thread found\n", __func__);
        printf("%s: No export thread found\n", __func__);
        return false;
    }

    // we need to get the last import transaction and handle refunds like exports, so we can make incremental progress if necessary and
    // know when there are none left
    int32_t nHeight = chainActive.Height();

    // make sure the chain is qualified for a refund
    if (!(chainDef.minpreconvert && chainDef.startBlock < nHeight && GetInitialCurrencyState(chainDef, defHeight).Reserve < chainDef.minpreconvert))
    {
        errorReason = "chain-ineligible";
        return false;
    }
    else
    {
        // convert exports intended for the failed chain into imports back to this chain that spend from the import thread
        // all reservetransfers are refunded and charged 1/2 of an export fee
        uint160 chainID = chainDef.GetChainID();

        std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

        // get all exports for the chain
        CKeyID keyID = CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_EXPORT);

        CBlockIndex *pIndex;

        // get all export transactions that were posted to the chain, since none can be sent
        // TODO:PBAAS - all sends to a failed chain after start block should fail. this may not want to
        // refund all exports, in case we decide to reuse chain names
        if (GetAddressIndex(keyID, 1, addressIndex, defHeight, nHeight))
        {
            // get all exports from first to last, and make sure they spend from the export thread consecutively
            bool found = false;
            uint256 lastHash = lastExportHash;

            // indexed by input hash
            std::map<uint256, std::pair<CAddressIndexKey, CTransaction>> validExports;

            // validate, order, and relate them with their inputs
            for (auto utxo : addressIndex)
            {
                // if current tx spends lastHash, then we have our next valid transaction to create an import with
                CTransaction tx, inputtx;
                uint256 blkHash1, blkHash2;
                BlockMap::iterator blkIt;
                CCrossChainExport ccx;
                COptCCParams p;
                if (!utxo.first.spending &&
                    !(utxo.first.txhash == lastExportHash) &&
                    myGetTransaction(utxo.first.txhash, tx, blkHash1) &&
                    (ccx = CCrossChainExport(tx)).IsValid() &&
                    ccx.numInputs &&
                    (!tx.IsCoinBase() && 
                    tx.vin.size() && 
                    myGetTransaction(tx.vin[0].prevout.hash, inputtx, blkHash2)) &&
                    inputtx.vout[tx.vin[0].prevout.n].scriptPubKey.IsPayToCryptoCondition(p) && 
                    p.IsValid() && 
                    p.evalCode == EVAL_CROSSCHAIN_EXPORT)
                {
                    validExports.insert(make_pair(tx.vin[0].prevout.hash, make_pair(utxo.first, tx)));
                }
            }

            CTransaction lastImport(lastImportTx);
            CCrossChainImport lastCCI;

            for (auto aixIt = validExports.find(lastExportHash); 
                aixIt != validExports.end(); 
                aixIt = validExports.find(lastExportHash))
            {
                // One pass - create an import transaction that spends the last import transaction from a confirmed export transaction that spends the last one of those
                // 1. Creates preconvert outputs that spend from the initial supply, which comes from the import transaction thread without fees
                // 2. Creates reserve outputs for unconverted imports
                // 3. Creates reserveExchange transactions requesting conversion at market for convert transfers
                // 4. Creates reserveTransfer outputs for outputs with the SEND_BACK flag set, unless they are under 5x the normal network fee
                // 5. Creates a pass-through EVAL_CROSSCHAIN_IMPORT output with the remainder of the non-preconverted coins
                // then signs the transaction considers it the latest import and the new export the latest export and
                // loops until there are no more confirmed, consecutive, valid export transactions to export

                // aixIt has an input from the export thread of last transaction, an optional deposit to the reserve, and an opret of all outputs + 1 fee
                assert(aixIt->second.second.vout.back().scriptPubKey.IsOpReturn());

                int32_t lastImportPrevoutN = 0, lastExportPrevoutN = 0;
                lastCCI = CCrossChainImport();
                COptCCParams p;

                for (int i = 0; i < lastImport.vout.size(); i++)
                {
                    if (lastImport.vout[i].scriptPubKey.IsPayToCryptoCondition(p) && p.IsValid() && p.evalCode == EVAL_CROSSCHAIN_IMPORT && p.vData.size() && (lastCCI = CCrossChainImport(p.vData[0])).IsValid())
                    {
                        lastImportPrevoutN = i;
                        break;
                    }
                }

                CCrossChainExport ccx(aixIt->second.second);
                if (!lastCCI.IsValid() || !ccx.IsValid())
                {
                    LogPrintf("%s: POSSIBLE CORRUPTION bad import/export data in transaction %s (import) or %s (export)\n", __func__, lastImport.GetHash().GetHex().c_str(), aixIt->first.GetHex().c_str());
                    printf("%s: POSSIBLE CORRUPTION bad import/export data transaction %s (import) or %s (export)\n", __func__, lastImport.GetHash().GetHex().c_str(), aixIt->first.GetHex().c_str());
                    return false;
                }

                CMutableTransaction newImportTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight);
                newImportTx.vin.push_back(CTxIn(lastImport.GetHash(), lastImportPrevoutN));

                // if no prepared input, make one
                newImportTx.vout.clear();
                newImportTx.vout.push_back(CTxOut()); // placeholder for the first output

                // we need to recalculate fees, as there will be no conversions

                CReserveTransactionDescriptor rtxd;

                std::vector<CBaseChainObject *> exportOutputs = RetrieveOpRetArray(aixIt->second.second.vout.back().scriptPubKey);

                // loop through and recalculate every transfer to charge a normal transfer fee and be a normal output
                // recalculate the fee output amount to be based on the new export fee calculation after canceling conversions

                ccx.totalFees = (CReserveTransfer::DEFAULT_PER_STEP_FEE << 1) * ccx.numInputs;
                for (auto objPtr : exportOutputs)
                {
                    if (objPtr->objectType == CHAINOBJ_RESERVETRANSFER)
                    {
                        CReserveTransfer &rt = ((CChainObject<CReserveTransfer> *)objPtr)->object;

                        // turn it into a normal transfer, which will create a regular native output
                        rt.flags &= ~(CReserveTransfer::SEND_BACK | CReserveTransfer::PRECONVERT | CReserveTransfer::CONVERT);

                        if (rt.flags & CReserveTransfer::FEE_OUTPUT)
                        {
                            // will be recalculated
                            rt.nFees = 0;
                            rt.nValue = ccx.CalculateExportFee();
                            continue;
                        }
                        else
                        {
                            rt.nValue = (rt.nValue + rt.nFees) - (CReserveTransfer::DEFAULT_PER_STEP_FEE << 1);
                            rt.nFees = CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;
                        }
                    }
                }

                if (!rtxd.AddReserveTransferImportOutputs(ConnectedChains.ThisChain(), exportOutputs, newImportTx.vout))
                {
                    LogPrintf("%s: POSSIBLE CORRUPTION bad export opret in transaction %s\n", __func__, aixIt->second.second.GetHash().GetHex().c_str());
                    printf("%s: POSSIBLE CORRUPTION bad export opret in transaction %s\n", __func__, aixIt->second.second.GetHash().GetHex().c_str());
                    // free the memory
                    DeleteOpRetObjects(exportOutputs);
                    return false;
                }

                // free the memory
                DeleteOpRetObjects(exportOutputs);

                // now add any unspent reserve deposit that came from this export to return it back
                std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > reserveDeposits;
                CAmount totalInput = lastImport.vout[lastImportPrevoutN].nValue;
                if (GetAddressUnspent(CKeyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_RESERVE_DEPOSIT)), 1, reserveDeposits))
                {
                    for (auto deposit : reserveDeposits)
                    {
                        COptCCParams p;
                        if (deposit.second.script.IsPayToCryptoCondition(p) && p.evalCode == EVAL_RESERVE_DEPOSIT)
                        {
                            newImportTx.vin.push_back(CTxIn(deposit.first.txhash, deposit.first.index, CScript()));
                            totalInput += deposit.second.satoshis;
                        }
                    }
                }

                CAmount totalAvailableInput = totalInput;
                CAmount availableReserveFees = ccx.totalFees;
                CAmount exportFees = ccx.CalculateExportFee();
                CAmount importFees = ccx.CalculateImportFee();
                CAmount feesOut = 0;

                CCcontract_info CC;
                CCcontract_info *cp;
                CPubKey pk;

                // emit a crosschain import output as summary
                cp = CCinit(&CC, EVAL_CROSSCHAIN_IMPORT);

                pk = CPubKey(ParseHex(CC.CChexstr));

                if (rtxd.ReserveFees() != (ccx.totalFees - exportFees))
                {
                    LogPrintf("%s: ERROR - refund does not match amount, rtxd.ReserveFees()=%lu, totalImport=%lu, importFees=%lu, exportFees=%lu, ccx.totalAmount=%lu, ccx.totalFees=%lu\n", __func__, rtxd.ReserveFees(), ccx.totalAmount + ccx.totalFees, importFees, exportFees, ccx.totalAmount, ccx.totalFees);
                    printf("%s: ERROR - refund does not match amount, rtxd.ReserveFees()=%lu, totalImport=%lu, importFees=%lu, exportFees=%lu, ccx.totalAmount=%lu, ccx.totalFees=%lu\n", __func__, rtxd.ReserveFees(), ccx.totalAmount + ccx.totalFees, importFees, exportFees, ccx.totalAmount, ccx.totalFees);
                }

                std::vector<CTxDestination> dests = std::vector<CTxDestination>({CTxDestination(CKeyID(CCrossChainRPCData::GetConditionID(chainID, EVAL_CROSSCHAIN_IMPORT)))});
                CCrossChainImport cci = CCrossChainImport(chainID, ccx.totalAmount + ccx.totalFees);

                totalAvailableInput -= rtxd.reserveIn;
                newImportTx.vout[0] = MakeCC1of1Vout(EVAL_CROSSCHAIN_IMPORT, totalAvailableInput, pk, dests, cci);

                if (totalAvailableInput < 0)
                {
                    LogPrintf("%s: ERROR - importing more native currency than available on import thread\n", __func__);
                    printf("%s: ERROR - importing more native currency than available on import thread\n", __func__);
                    return false;
                }

                // add the opret, which is the export transaction this imports
                std::vector<CBaseChainObject *> chainObjects;
                CChainObject<CTransaction> exportTx(CHAINOBJ_TRANSACTION, aixIt->second.second);
                chainObjects.push_back(&exportTx);

                // add a proof of the export transaction at the notarization height
                CBlock block;
                if (!ReadBlockFromDisk(block, chainActive[aixIt->second.first.blockHeight], Params().GetConsensus(), false))
                {
                    LogPrintf("%s: POSSIBLE CORRUPTION cannot read block %s\n", __func__, chainActive[aixIt->second.first.blockHeight]->GetBlockHash().GetHex().c_str());
                    printf("%s: POSSIBLE CORRUPTION cannot read block %s\n", __func__, chainActive[aixIt->second.first.blockHeight]->GetBlockHash().GetHex().c_str());
                    return false;
                }

                CMerkleBranch exportProof(aixIt->second.first.index, block.GetMerkleBranch(aixIt->second.first.index));
                auto mmrView = chainActive.GetMMV();
                mmrView.resize(aixIt->second.first.blockHeight);

                printf("%s: created refund %s for export %s\n", __func__, cci.ToUniValue().write().c_str(), aixIt->second.second.GetHash().GetHex().c_str());

                chainActive[aixIt->second.first.blockHeight]->AddMerkleProofBridge(exportProof);
                mmrView.GetProof(exportProof, aixIt->second.first.blockHeight);

                CChainObject<CCrossChainProof> exportXProof(CHAINOBJ_CROSSCHAINPROOF, CCrossChainProof(uint256(), exportProof));
                chainObjects.push_back(&exportXProof);

                // add the opret with the transaction and its proof that references the notarization with the correct MMR
                newImportTx.vout.push_back(CTxOut(0, StoreOpRetArray(chainObjects)));

                // we now have an Import transaction for the chainID chain, it is the latest, and the export we used is now the latest as well
                // work our way forward
                newRefunds.push_back(newImportTx);
                lastImport = newImportTx;
                lastExportHash = aixIt->second.first.txhash;
            }
        }
        return true;
    }
}

UniValue refundfailedlaunch(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "refundfailedlaunch \"chainid\"\n"
            "\nRefunds any funds sent to the chain if they are eligible for refund.\n"
            "This attempts to refund all transactions for all contributors.\n"

            "\nArguments\n"
            "\"chainid\"            (hex or chain name, required)   the chain to refund contributions to\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("refundfailedlaunch", "\"chainid\"")
            + HelpExampleRpc("refundfailedlaunch", "\"chainid\"")
        );
    }
    CheckPBaaSAPIsValid();

    uint160 chainID;

    chainID = GetChainIDFromParam(params[0]);
    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid PBaaS name or chainid");
    }

    if (chainID == ConnectedChains.ThisChain().GetChainID() || chainID == ConnectedChains.NotaryChain().chainDefinition.GetChainID())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot refund the specified chain");
    }

    CTransaction lastImportTx;
    std::vector<CTransaction> refundTxes;
    std::string failReason;

    if (!RefundFailedLaunch(chainID, lastImportTx, refundTxes, failReason))
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, failReason);
    }

    uint32_t consensusBranchId = CurrentEpochBranchId(chainActive.LastTip()->GetHeight(), Params().GetConsensus());

    UniValue ret(UniValue::VARR);

    CCoinsViewCache view(pcoinsTip);

    // sign and commit the transactions
    for (auto tx : refundTxes)
    {
        LOCK2(cs_main, mempool.cs);

        CMutableTransaction newTx(tx);

        // sign the transaction and submit
        bool signSuccess;
        for (int i = 0; i < tx.vin.size(); i++)
        {
            SignatureData sigdata;
            CAmount value;
            CScript outputScript;

            if (tx.vin[i].prevout.hash == lastImportTx.GetHash())
            {
                value = lastImportTx.vout[tx.vin[i].prevout.n].nValue;
                outputScript = lastImportTx.vout[tx.vin[i].prevout.n].scriptPubKey;
            }
            else
            {
                CCoinsViewCache view(pcoinsTip);
                CCoins coins;
                if (!view.GetCoins(tx.vin[i].prevout.hash, coins))
                {
                    fprintf(stderr,"refundfailedlaunch: cannot get input coins from tx: %s, output: %d\n", tx.vin[i].prevout.hash.GetHex().c_str(), tx.vin[i].prevout.n);
                    LogPrintf("refundfailedlaunch: cannot get input coins from tx: %s, output: %d\n", tx.vin[i].prevout.hash.GetHex().c_str(), tx.vin[i].prevout.n);
                    break;
                }
                value = coins.vout[tx.vin[i].prevout.n].nValue;
                outputScript = coins.vout[tx.vin[i].prevout.n].scriptPubKey;
            }

            signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &tx, i, value, SIGHASH_ALL), outputScript, sigdata, consensusBranchId);

            if (!signSuccess)
            {
                fprintf(stderr,"refundfailedlaunch: failure to sign refund transaction\n");
                LogPrintf("refundfailedlaunch: failure to sign refund transaction\n");
                break;
            } else {
                UpdateTransaction(newTx, i, sigdata);
            }
        }

        if (signSuccess)
        {
            // push to local node and sync with wallets
            CValidationState state;
            bool fMissingInputs;
            CTransaction signedTx(newTx);
            if (!AcceptToMemoryPool(mempool, state, signedTx, false, &fMissingInputs)) {
                if (state.IsInvalid()) {
                    fprintf(stderr,"refundfailedlaunch: rejected by memory pool for %s\n", state.GetRejectReason().c_str());
                    LogPrintf("refundfailedlaunch: rejected by memory pool for %s\n", state.GetRejectReason().c_str());
                } else {
                    if (fMissingInputs) {
                        fprintf(stderr,"refundfailedlaunch: missing inputs\n");
                        LogPrintf("refundfailedlaunch: missing inputs\n");
                    }
                    else
                    {
                        fprintf(stderr,"refundfailedlaunch: rejected by memory pool for\n");
                        LogPrintf("refundfailedlaunch: rejected by memory pool for\n");
                    }
                }
                break;
            }
            else
            {
                RelayTransaction(signedTx);
                ret.push_back(signedTx.GetHash().GetHex());
            }
        }
    }
    return ret;
}

UniValue getinitialcurrencystate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getinitialcurrencystate \"name\"\n"
            "\nReturns the total amount of preconversions that have been confirmed on the blockchain for the specified chain.\n"

            "\nArguments\n"
            "   \"name\"                    (string, required) name or chain ID of the chain to get the export transactions for\n"

            "\nResult:\n"
            "   [\n"
            "       {\n"
            "           \"flags\" : n,\n"
            "           \"initialratio\" : n,\n"
            "           \"initialsupply\" : n,\n"
            "           \"emitted\" : n,\n"
            "           \"supply\" : n,\n"
            "           \"reserve\" : n,\n"
            "           \"currentratio\" : n,\n"
            "       },\n"
            "   ]\n"

            "\nExamples:\n"
            + HelpExampleCli("getinitialcurrencystate", "name")
            + HelpExampleRpc("getinitialcurrencystate", "name")
        );
    }
    CheckPBaaSAPIsValid();

    uint160 chainID = GetChainIDFromParam(params[0]);

    if (chainID.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain name or chain ID");
    }

    CPBaaSChainDefinition chainDef;
    int32_t definitionHeight;
    if (!GetChainDefinition(chainID, chainDef, &definitionHeight))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain " + params[0].get_str() + " not found");
    }

    return GetInitialCurrencyState(chainDef, definitionHeight).ToUniValue();
}

UniValue getcurrencystate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getcurrencystate \"n\"\n"
            "\nReturns the total amount of preconversions that have been confirmed on the blockchain for the specified chain.\n"

            "\nArguments\n"
            "   \"n\" or \"m,n\" or \"m,n,o\"         (int or string, optional) height or inclusive range with optional step at which to get the currency state\n"
            "                                                                   If not specified, the latest currency state and height is returned\n"

            "\nResult:\n"
            "   [\n"
            "       {\n"
            "           \"height\": n,\n"
            "           \"blocktime\": n,\n"
            "           \"currencystate\": {\n"
            "               \"flags\" : n,\n"
            "               \"initialratio\" : n,\n"
            "               \"initialsupply\" : n,\n"
            "               \"emitted\" : n,\n"
            "               \"supply\" : n,\n"
            "               \"reserve\" : n,\n"
            "               \"currentratio\" : n,\n"
            "           \"}\n"
            "       },\n"
            "   ]\n"

            "\nExamples:\n"
            + HelpExampleCli("getcurrencystate", "name")
            + HelpExampleRpc("getcurrencystate", "name")
        );
    }
    CheckPBaaSAPIsValid();

    uint64_t lStart;
    uint64_t startEnd[3] = {0};

    lStart = startEnd[1] = startEnd[0] = chainActive.LastTip() ? chainActive.LastTip()->GetHeight() : 1;

    if (params.size() == 1)
    {
        if (uni_get_int(params[0], -1) == -1 && params[0].isStr())
        {
            Split(params[0].get_str(), startEnd, startEnd[0], 3);
        }
    }

    if (startEnd[0] > startEnd[1])
    {
        startEnd[0] = startEnd[1];
    }

    if (startEnd[1] > lStart)
    {
        startEnd[1] = lStart;
    }

    if (startEnd[1] < startEnd[0])
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block range for currency state");
    }

    if (startEnd[2] == 0)
    {
        startEnd[2] = 1;
    }

    if (startEnd[2] > INT_MAX)
    {
        startEnd[2] = INT_MAX;
    }

    uint32_t start = startEnd[0], end = startEnd[1], step = startEnd[2];

    UniValue ret(UniValue::VARR);

    for (int i = start; i <= end; i += step)
    {
        CCoinbaseCurrencyState currencyState = ConnectedChains.GetCurrencyState(i);
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("height", i));
        entry.push_back(Pair("blocktime", (uint64_t)chainActive.LastTip()->nTime));
        CAmount price;
        entry.push_back(Pair("currencystate", currencyState.ToUniValue()));
        ret.push_back(entry);
    }
    return ret;
}

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

UniValue definechain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "definechain '{\"name\": \"BAAS\", ... }'\n"
            "\nThis defines a PBaaS chain, provides it with initial notarization fees to support its launch, and prepares it to begin running.\n"

            "\nArguments\n"
            "      {\n"
            "         \"name\"       : \"xxxx\",    (string, required) unique Verus ecosystem-wide name/symbol of this PBaaS chain\n"
            "         \"paymentaddress\" : \"Rxxx\", (string, optional) premine and launch fee recipient\n"
            "         \"premine\"    : \"n\",       (int,    optional) amount of coins that will be premined and distributed to premine address\n"
            "         \"initialcontribution\" : \"n\", (int, optional) amount of coins as initial contribution\n"
            "         \"conversion\" : \"n\",       (int,    optional) amount of coins that may be converted from Verus, price determined by total contribution\n"
            "         \"launchfee\"  : \"n\",       (int,    optional) VRSC fee for conversion at startup, multiplied by amount, divided by 100000000\n"
            "         \"startblock\" : \"n\",       (int,    optional) VRSC block must be notarized into block 1 of PBaaS chain, default curheight + 100\n"
            "         \"eras\"       : \"objarray\", (array, optional) data specific to each era, maximum 3\n"
            "         {\n"
            "            \"reward\"      : \"n\",   (int64,  optional) native initial block rewards in each period\n"
            "            \"decay\" : \"n\",         (int64,  optional) reward decay for each era\n"
            "            \"halving\"      : \"n\",  (int,    optional) halving period for each era\n"
            "            \"eraend\"       : \"n\",  (int,    optional) ending block of each era\n"
            "            \"eraoptions\"   : \"n\",  (int,    optional) options for each era\n"
            "         }\n"
            "         \"notarizationreward\" : \"n\", (int,  required) default VRSC notarization reward total for first billing period\n"
            "         \"billingperiod\" : \"n\",    (int,    optional) number of blocks in each billing period\n"
            "         \"nodes\"      : \"[obj, ..]\", (objectarray, optional) up to 2 nodes that can be used to connect to the blockchain"
            "         [{\n"
            "            \"networkaddress\" : \"txid\", (string,  optional) internet, TOR, or other supported address for node\n"
            "            \"paymentaddress\" : \"n\", (int,     optional) rewards payment address\n"
            "          }, .. ]\n"
            "      }\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"transactionid\", (string) The transaction id\n"
            "  \"tx\"   : \"json\",          (json)   The transaction decoded as a transaction\n"
            "  \"hex\"  : \"data\"           (string) Raw data for signed transaction\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("definechain", "jsondefinition")
            + HelpExampleRpc("definechain", "jsondefinition")
        );
    }

    CheckPBaaSAPIsValid();

    if (!params[0].isObject())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "JSON object required. see help.");
    }
    if (!pwalletMain)
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "must have active wallet to define PBaaS chain");
    }

    UniValue valStr(UniValue::VSTR);
    if (!valStr.read(params[0].write()))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid characters in blockchain definition");
    }

    CPBaaSChainDefinition newChain(params[0]);

    if (!newChain.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid chain definition. see help.");
    }

    CPBaaSChainDefinition checkDef;
    if (GetChainDefinition(newChain.name, checkDef))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, newChain.name + " chain already defined. see help.");
    }

    if (!newChain.startBlock || newChain.startBlock < (chainActive.Height() + PBAAS_MINSTARTBLOCKDELTA))
    {
        newChain.startBlock = chainActive.Height() + PBAAS_MINSTARTBLOCKDELTA;
    }

    if (newChain.billingPeriod < CPBaaSChainDefinition::MIN_BILLING_PERIOD || (newChain.notarizationReward / newChain.billingPeriod) < CPBaaSChainDefinition::MIN_PER_BLOCK_NOTARIZATION)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Billing period of at least " + 
                                               to_string(CPBaaSChainDefinition::MIN_BILLING_PERIOD) + 
                                               " blocks and per-block notary rewards of >= 1000000 are required to define a chain\n");
    }

    for (int i = 0; i < newChain.rewards.size(); i++)
    {
        arith_uint256 reward(newChain.rewards[i]), decay(newChain.rewardsDecay[i]), limit(0x7fffffffffffffff);
        if (reward * decay > limit)
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "reward * decay exceeds 64 bit integer limit of 9,223,372,036,854,775,807\n");
        }
    }

    vector<CRecipient> outputs;

    // default double fee for miner of chain definition tx
    // one output for definition, one for finalization
    CAmount nReward = newChain.notarizationReward + (DEFAULT_TRANSACTION_FEE * 4);

    CCcontract_info CC;
    CCcontract_info *cp;

    // make the chain definition output
    cp = CCinit(&CC, EVAL_PBAASDEFINITION);
    // need to be able to send this to EVAL_PBAASDEFINITION address as a destination, locked by the default pubkey
    CPubKey pk(ParseHex(CC.CChexstr));

    std::vector<CTxDestination> dests({CKeyID(CCrossChainRPCData::GetConditionID(ConnectedChains.ThisChain().GetChainID(), EVAL_PBAASDEFINITION))});
    CTxOut defOut = MakeCC1of1Vout(EVAL_PBAASDEFINITION, DEFAULT_TRANSACTION_FEE, pk, dests, newChain);
    outputs.push_back(CRecipient({defOut.scriptPubKey, CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE, false}));

    // make the first chain notarization output
    cp = CCinit(&CC, EVAL_ACCEPTEDNOTARIZATION);

    // we need to make a notarization, notarize this information and block 0, since we know that will be in the new
    // chain, our authorization will be that we are the chain definition
    uint256 mmvRoot, nodePreHash;
    {
        LOCK(cs_main);
        auto mmr = chainActive.GetMMR();
        auto mmv = CMerkleMountainView<CMMRPowerNode, CChunkedLayer<CMMRPowerNode>, COverlayNodeLayer<CMMRPowerNode, CChain>>(mmr, mmr.size());
        mmv.resize(1);
        mmvRoot = mmv.GetRoot();
        nodePreHash = mmr.GetNode(0).hash;
    }

    CKeyID pkID;
    extern int32_t USE_EXTERNAL_PUBKEY; extern std::string NOTARY_PUBKEY;
    if (USE_EXTERNAL_PUBKEY)
    {
        CPubKey pubKey = CPubKey(ParseHex(NOTARY_PUBKEY));
        if (pubKey.IsFullyValid())
        {
            pkID = pubKey.GetID();
        }
    }

    CAmount initialToConvert = 0;
    CAmount initialConversion = 0;
    CAmount initialReserve = 0;
    CAmount initialFee = 0;
    CAmount conversionFee = 0;

    // add initial conversion payment if present for reserve to reserve
    // any initial contribution must be at least 1 coin
    if (newChain.initialcontribution > COIN)
    {
        arith_uint256 bigConvert(newChain.initialcontribution);
        static const arith_uint256 bigsatoshi(CReserveExchange::SATOSHIDEN);
        initialFee = ((bigConvert * arith_uint256(newChain.launchFee)) / bigsatoshi).GetLow64();
        initialToConvert = newChain.initialcontribution - initialFee;
        bigConvert = arith_uint256(initialToConvert);
        initialConversion = ((bigConvert * arith_uint256(newChain.conversion)) / bigsatoshi).GetLow64();
        // if a reserve chain, converted amount goes to reserves
        if (!(newChain.ChainOptions() & CPBaaSChainDefinition::OPTION_RESERVE))
        {
            initialReserve = initialToConvert;
        }
    }
    else
    {
        newChain.initialcontribution = 0;
    }
    
    CCurrencyState currencyState(newChain.conversion, newChain.premine + initialConversion, initialConversion, 0, initialReserve);

    CPBaaSNotarization pbn = CPBaaSNotarization(CPBaaSNotarization::CURRENT_VERSION,
                                                newChain.GetChainID(),
                                                pkID,
                                                0, mmvRoot,
                                                nodePreHash,
                                                ArithToUint256(GetCompactPower(chainActive.Genesis()->nNonce, chainActive.Genesis()->nBits, chainActive.Genesis()->nVersion)),
                                                currencyState,
                                                uint256(), 0,
                                                uint256(), 0,
                                                COpRetProof(),
                                                newChain.nodes);

    pk = CPubKey(ParseHex(CC.CChexstr));
    dests = std::vector<CTxDestination>({CKeyID(newChain.GetConditionID(EVAL_ACCEPTEDNOTARIZATION))});
    CTxOut notarizationOut = MakeCC1of1Vout(EVAL_ACCEPTEDNOTARIZATION, nReward, pk, dests, pbn);
    outputs.push_back(CRecipient({notarizationOut.scriptPubKey, newChain.notarizationReward, false}));

    // make the finalization output
    cp = CCinit(&CC, EVAL_FINALIZENOTARIZATION);
    pk = CPubKey(ParseHex(CC.CChexstr));
    dests = std::vector<CTxDestination>({CKeyID(newChain.GetConditionID(EVAL_FINALIZENOTARIZATION))});

    CNotarizationFinalization nf;
    CTxOut finalizationOut = MakeCC1of1Vout(EVAL_FINALIZENOTARIZATION, DEFAULT_TRANSACTION_FEE, pk, dests, nf);
    outputs.push_back(CRecipient({finalizationOut.scriptPubKey, CPBaaSChainDefinition::DEFAULT_OUTPUT_VALUE, false}));

    // create import and export threads
    // import - only spendable for reserve currency or currency with preconversion to allow import of conversions, this output will include
    // all pre-converted coins
    // chain definition - always
    // make the chain definition output
    dests.clear();
    cp = CCinit(&CC, EVAL_CROSSCHAIN_IMPORT);

    pk = CPubKey(ParseHex(CC.CChexstr));

    CKeyID newChainID(newChain.GetChainID());

    // import thread is specific to the chain importing from
    dests.push_back(CKeyID(CCrossChainRPCData::GetConditionID(newChainID, EVAL_CROSSCHAIN_IMPORT)));

    CTxOut importThreadOut = MakeCC1of1Vout(EVAL_CROSSCHAIN_IMPORT, DEFAULT_TRANSACTION_FEE, pk, dests, CCrossChainImport(newChainID, 0));
    outputs.push_back(CRecipient({importThreadOut.scriptPubKey, DEFAULT_TRANSACTION_FEE, false}));

    // export - currently only spendable for reserve currency, but added for future capabilities
    dests.clear();
    cp = CCinit(&CC, EVAL_CROSSCHAIN_EXPORT);

    pk = CPubKey(ParseHex(CC.CChexstr));
    dests.push_back(CKeyID(CCrossChainRPCData::GetConditionID(newChainID, EVAL_CROSSCHAIN_EXPORT)));

    CTxOut exportThreadOut = MakeCC1of1Vout(EVAL_CROSSCHAIN_EXPORT, DEFAULT_TRANSACTION_FEE, pk, dests, CCrossChainExport(newChainID, 0, 0, 0));
    outputs.push_back(CRecipient({exportThreadOut.scriptPubKey, DEFAULT_TRANSACTION_FEE, false}));

    // make the reserve transfer and fee outputs if there is an initial contribution
    if (newChain.initialcontribution)
    {
        cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);
        pk = CPubKey(ParseHex(CC.CChexstr));
        assert(pk.IsFullyValid());

        dests = std::vector<CTxDestination>({CKeyID(ConnectedChains.ThisChain().GetConditionID(EVAL_RESERVE_TRANSFER)), CKeyID(newChainID)});

        CAmount fee = CReserveTransactionDescriptor::CalculateAdditionalConversionFee(initialToConvert);
        CAmount contribution = (initialToConvert + fee) - CReserveTransactionDescriptor::CalculateConversionFee(initialToConvert + fee);
        fee += CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;

        CReserveTransfer rt = CReserveTransfer(CReserveTransfer::PRECONVERT + CReserveTransfer::VALID, contribution, fee, newChain.address);
        CTxOut reserveTransferOut = MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, initialToConvert + fee, pk, dests, (CReserveTransfer)rt);
        outputs.push_back(CRecipient({reserveTransferOut.scriptPubKey, initialToConvert + fee, false}));

        // if there is a fee output, send it to the payment address
        if (initialFee)
        {
            CTxDestination feeOutAddr(newChain.address);
            outputs.push_back(CRecipient({GetScriptForDestination(feeOutAddr), initialFee, false}));
        }
    }

    // create the transaction
    CWalletTx wtx;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        CReserveKey reserveKey(pwalletMain);
        CAmount fee;
        int nChangePos;
        string failReason;

        if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
        {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, newChain.name + ": " + failReason);
        }
    }

    UniValue uvret(UniValue::VOBJ);
    uvret.push_back(Pair("chaindefinition", CPBaaSChainDefinition(wtx).ToUniValue()));

    uvret.push_back(Pair("basenotarization", CPBaaSNotarization(wtx).ToUniValue()));

    uvret.push_back(Pair("txid", wtx.GetHash().GetHex()));

    UniValue txJSon(UniValue::VOBJ);
    TxToJSON(wtx, uint256(), txJSon);
    uvret.push_back(Pair("tx",  txJSon));

    string strHex = EncodeHexTx(static_cast<CTransaction>(wtx));
    uvret.push_back(Pair("hex", strHex));

    return uvret;
}

UniValue registernamecommitment(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() < 2 && params.size() > 3))
    {
        throw runtime_error(
            "registernamecommitment \"name\" \"controladdress\" (\"referralidentity\")\n"
            "\nRegisters a name commitment, which is required as a source for the name to be used when registering an identity. The name commitment hides the name itself\n"
            "while ensuring that the miner who mines in the registration cannot front-run the name unless they have also registered a name commitment for the same name or\n"
            "are willing to forfeit the offer of payment for the chance that a commitment made now will allow them to register the name in the future.\n"

            "\nArguments\n"
            "\"name\"                           (string, required)  the unique name to commit to. creating a name commitment is not a registration, and if one is\n"
            "                                                       created for a name that exists, it may succeed, but will never be able to be used.\n"
            "\"controladdress\"                 (address, required) address that will control this commitment\n"
            "\"referralidentity\"               (identity, optional)friendly name or identity address that is provided as a referral mechanism and to lower network cost of the ID\n"

            "\nResult: obj\n"
            "{\n"
            "    \"txid\" : \"hexid\"\n"
            "    \"namereservation\" :\n"
            "    {\n"
            "        \"name\"    : \"namestr\",     (string) the unique name in this commitment\n"
            "        \"salt\"    : \"hexstr\",      (hex)    salt used to hide the commitment\n"
            "        \"referral\": \"identityaddress\", (base58) address of the referring identity if there is one\n"
            "        \"parent\"  : \"namestr\",   (string) name of the parent if not Verus or Verus test\n"
            "        \"nameid\"  : \"address\",   (base58) identity address for this identity if it is created\n"
            "    }\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("registernamecommitment", "\"name\"")
            + HelpExampleRpc("registernamecommitment", "\"name\"")
        );
    }

    CheckIdentityAPIsValid();

    uint160 parent;
    std::string name = CleanName(uni_get_str(params[0]), parent, true);

    // if either we have an invalid name or an implied parent, that is not valid
    if (name == "" || !parent.IsNull() || name != uni_get_str(params[0]))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid name for commitment. Names must not have leading or trailing spaces and must not include any of the following characters between parentheses (\\/:*?\"<>|@)");
    }

    parent = ConnectedChains.ThisChain().GetChainID();

    CTxDestination dest = DecodeDestination(uni_get_str(params[1]));
    if (dest.which() == COptCCParams::ADDRTYPE_INVALID)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid control address for commitment");
    }

    // create the transaction with native coin as input
    LOCK2(cs_main, pwalletMain->cs_wallet);

    CIdentityID referrer;
    if (params.size() > 2)
    {
        CTxDestination referDest = DecodeDestination(uni_get_str(params[2]));
        if (referDest.which() != COptCCParams::ADDRTYPE_ID)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid referral identity for commitment, must be a currently registered friendly name or i-address");
        }
        referrer = CIdentityID(GetDestinationID(referDest));
        if (!CIdentity::LookupIdentity(referrer).IsValidUnrevoked())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Referral identity for commitment must be a currently valid, unrevoked friendly name or i-address");
        }
    }

    CNameReservation nameRes(name, referrer, GetRandHash());
    CCommitmentHash commitment(nameRes.GetCommitment());
    
    CConditionObj<CCommitmentHash> condObj(EVAL_IDENTITY_COMMITMENT, std::vector<CTxDestination>({dest}), 1, &commitment);
    std::vector<CRecipient> outputs = std::vector<CRecipient>({{MakeMofNCCScript(condObj, &dest), CCommitmentHash::DEFAULT_OUTPUT_AMOUNT, false}});
    CWalletTx wtx;

    if (CIdentity::LookupIdentity(CIdentity::GetID(name, parent)).IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Identity already exists.");
    }

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to create commitment transaction: " + failReason);
    }
    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
    ret.push_back(Pair("namereservation", nameRes.ToUniValue()));
    return ret;
}

UniValue registeridentity(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "registeridentity \"jsonidregistration\" feeoffer\n"
            "\n\n"

            "\nArguments\n"
            "{\n"
            "    \"txid\" : \"hexid\",          (hex)    the transaction ID of the name committment for this ID name\n"
            "    \"namereservation\" :\n"
            "    {\n"
            "        \"name\": \"namestr\",     (string) the unique name in this commitment\n"
            "        \"salt\": \"hexstr\",      (hex)    salt used to hide the commitment\n"
            "        \"referrer\": \"identityID\", (name@ or address) must be a valid ID to use as a referrer to receive a discount\n"
            "    },\n"
            "    \"identity\" :\n"
            "    {\n"
            "        \"name\": \"namestr\",     (string) the unique name for this identity\n"
            "        ...\n"
            "    }\n"
            "}\n"
            "feeoffer                           (amount, optional) amount to offer miner/staker for the registration fee, if missing, uses standard price\n\n"

            "\nResult:\n"
            "   transactionid                   (hexstr)\n"

            "\nExamples:\n"
            + HelpExampleCli("registeridentity", "jsonidregistration")
            + HelpExampleRpc("registeridentity", "jsonidregistration")
        );
    }

    CheckIdentityAPIsValid();

    // all names have a parent of the current chain
    uint160 parent = ConnectedChains.ThisChain().GetChainID();

    uint256 txid = uint256S(uni_get_str(find_value(params[0], "txid")));
    CNameReservation reservation(find_value(params[0], "namereservation"));

    CIdentity newID(find_value(params[0], "identity"));
    if (!newID.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid identity");
    }

    CAmount feeOffer;
    CAmount minFeeOffer = reservation.referral.IsNull() ? CIdentity::FullRegistrationAmount() : CIdentity::ReferredRegistrationAmount();

    if (params.size() > 1)
    {
        feeOffer = AmountFromValue(params[1]);
    }
    else
    {
        feeOffer = minFeeOffer;
    }

    if (feeOffer < minFeeOffer)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee offer must be at least " + ValueFromAmount(CIdentity::MinRegistrationAmount()).write());
    }

    uint160 impliedParent;
    if (txid.IsNull() || CleanName(reservation.name, impliedParent) != CleanName(newID.name, impliedParent) || !impliedParent.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid identity description or mismatched reservation.");
    }

    // lookup commitment to be sure that we can register this identity
    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hashBlk;
    uint32_t commitmentHeight;
    CTransaction txOut;
    CCommitmentHash ch;
    int commitmentOutput;

    // must be present and in a mined block
    {
        LOCK(mempool.cs);
        if (!myGetTransaction(txid, txOut, hashBlk) || hashBlk.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid or unconfirmed commitment transaction id");
        }

        auto indexIt = mapBlockIndex.find(hashBlk);
        if (indexIt == mapBlockIndex.end() || indexIt->second->GetHeight() > chainActive.Height() || chainActive[indexIt->second->GetHeight()]->GetBlockHash() != indexIt->second->GetBlockHash())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid or unconfirmed commitment");
        }

        commitmentHeight = indexIt->second->GetHeight();

        for (int i = 0; i < txOut.vout.size(); i++)
        {
            COptCCParams p;
            if (txOut.vout[i].scriptPubKey.IsPayToCryptoCondition(p) && p.IsValid() && p.evalCode == EVAL_IDENTITY_COMMITMENT && p.vData.size())
            {
                commitmentOutput = i;
                ::FromVector(p.vData[0], ch);
                break;
            }
        }
        if (ch.hash.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid commitment hash");
        }
    }

    if (ch.hash != reservation.GetCommitment().hash)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid commitment salt or referral ID");
    }

    // when creating an ID, the parent is always the current chains, and it is invalid to specify a parent
    if (!newID.parent.IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid to specify parent or qualified name when creating an identity. Parent is determined by the current blockchain.");
    }

    newID.parent = parent;

    CIdentity dupID = newID.LookupIdentity(newID.GetID());
    if (dupID.IsValid())
    {
        throw JSONRPCError(RPC_VERIFY_ALREADY_IN_CHAIN, "Identity already exists.");
    }

    // make sure we have a revocation and recovery authority defined
    CIdentity revocationAuth = (newID.revocationAuthority.IsNull() || newID.revocationAuthority == newID.GetID()) ? newID : newID.LookupIdentity(newID.revocationAuthority);
    CIdentity recoveryAuth = (newID.recoveryAuthority.IsNull() || newID.recoveryAuthority == newID.GetID()) ? newID : newID.LookupIdentity(newID.recoveryAuthority);

    if (!recoveryAuth.IsValidUnrevoked() || !revocationAuth.IsValidUnrevoked())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid or revoked recovery, or revocation identity.");
    }

    // now we have a new and valid primary, revocation, and recovery identity, as well as a valid reservation
    newID.revocationAuthority = revocationAuth.GetID();
    newID.recoveryAuthority = recoveryAuth.GetID();

    // create the identity definition transaction & reservation key output
    CConditionObj<CNameReservation> condObj(EVAL_IDENTITY_RESERVATION, std::vector<CTxDestination>({CIdentityID(newID.GetID())}), 1, &reservation);
    CTxDestination resIndexDest = CKeyID(CCrossChainRPCData::GetConditionID(newID.GetID(), EVAL_IDENTITY_RESERVATION));
    std::vector<CRecipient> outputs = std::vector<CRecipient>({{newID.IdentityUpdateOutputScript(), 0, false}});

    // add referrals, Verus supports referrals
    if ((ConnectedChains.ThisChain().IDReferrals() || IsVerusActive()) && !reservation.referral.IsNull())
    {
        uint32_t referralHeight;
        CTxIn referralTxIn;
        CTransaction referralIdTx;
        auto referralIdentity =  newID.LookupIdentity(reservation.referral, commitmentHeight - 1);
        if (referralIdentity.IsValidUnrevoked())
        {
            if (!newID.LookupFirstIdentity(reservation.referral, &referralHeight, &referralTxIn, &referralIdTx).IsValid())
            {
                throw JSONRPCError(RPC_DATABASE_ERROR, "Database or blockchain data error, \"" + referralIdentity.name + "\" seems valid, but first instance is not found in index");
            }

            // create outputs for this referral and up to n identities back in the referral chain
            outputs.push_back({referralIdentity.TransparentOutput(referralIdentity.GetID()), CIdentity::ReferralAmount(), false});
            feeOffer -= CIdentity::ReferralAmount();
            int afterId = referralTxIn.prevout.n + 1;
            for (int i = afterId; i < referralIdTx.vout.size() && (i - afterId) < (CIdentity::REFERRAL_LEVELS - 1); i++)
            {
                CTxDestination nextID;
                COptCCParams p, master;

                if (referralIdTx.vout[i].scriptPubKey.IsPayToCryptoCondition(p) && 
                    p.IsValid() && 
                    p.evalCode == EVAL_NONE && 
                    p.vKeys.size() == 1 && 
                    (p.vData.size() == 1 ||
                    (p.vData.size() == 2 && 
                    p.vKeys[0].which() == COptCCParams::ADDRTYPE_ID &&
                    (master = COptCCParams(p.vData[1])).IsValid() &&
                    master.evalCode == EVAL_NONE)))
                {
                    outputs.push_back({newID.TransparentOutput(CIdentityID(GetDestinationID(p.vKeys[0]))), CIdentity::ReferralAmount(), false});
                    feeOffer -= CIdentity::ReferralAmount();
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid or revoked referral identity at time of commitment");
        }
    }

    CScript reservationOutScript = MakeMofNCCScript(condObj, &resIndexDest);
    outputs.push_back({reservationOutScript, CNameReservation::DEFAULT_OUTPUT_AMOUNT, false});

    // make one dummy output, which CreateTransaction will leave as last, and we will remove to add its output to the fee
    // this serves to keep the change output after our real reservation output
    outputs.push_back({reservationOutScript, feeOffer, false});

    CWalletTx wtx;

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason, nullptr, false))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to create identity transaction: " + failReason);
    }

    // add commitment output
    CMutableTransaction mtx(wtx);
    mtx.vin.push_back(CTxIn(txid, commitmentOutput));

    // remove the fee offer output
    mtx.vout.pop_back();

    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    // now sign
    CCoinsViewCache view(pcoinsTip);
    for (int i = 0; i < wtx.vin.size(); i++)
    {
        bool signSuccess;
        SignatureData sigdata;

        CCoins coins;
        if (!(view.GetCoins(wtx.vin[i].prevout.hash, coins) && coins.IsAvailable(wtx.vin[i].prevout.n)))
        {
            break;
        }

        CAmount value = coins.vout[wtx.vin[i].prevout.n].nValue;

        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &wtx, i, value, coins.vout[wtx.vin[i].prevout.n].scriptPubKey), coins.vout[wtx.vin[i].prevout.n].scriptPubKey, sigdata, CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()));

        if (!signSuccess)
        {
            LogPrintf("%s: failure to sign identity registration tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            printf("%s: failure to sign identity registration tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, i, sigdata);
        }
    }
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }

    // including definitions and claims thread
    return UniValue(wtx.GetHash().GetHex());
}

UniValue updateidentity(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "updateidentity \"jsonidentity\" (returntx)\n"
            "\n\n"

            "\nArguments\n"
            "       \"returntx\"                        (bool,   optional) defaults to false and transaction is sent, if true, transaction is signed by this wallet and returned\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("updateidentity", "\'{\"name\" : \"myname\"}\'")
            + HelpExampleRpc("updateidentity", "\'{\"name\" : \"myname\"}\'")
        );
    }

    CheckIdentityAPIsValid();

    // get identity
    bool returnTx = false;
    CIdentity newID(params[0]);

    if (!newID.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid JSON ID parameter");
    }

    if (params.size() > 1)
    {
        returnTx = uni_get_bool(params[1], false);
    }

    CTxIn idTxIn;
    CIdentity oldID;
    uint32_t idHeight;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!(oldID = CIdentity::LookupIdentity(newID.GetID(), 0, &idHeight, &idTxIn)).IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ID not found " + newID.ToUniValue().write());
    }

    // create the identity definition transaction
    std::vector<CRecipient> outputs = std::vector<CRecipient>({{newID.IdentityUpdateOutputScript(), 0, false}});
    CWalletTx wtx;

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason, nullptr, false))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Unable to create update transaction: " + failReason);
    }

    CMutableTransaction mtx(wtx);
    mtx.vin.push_back(idTxIn);
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    // now sign
    CCoinsViewCache view(pcoinsTip);
    for (int i = 0; i < wtx.vin.size(); i++)
    {
        bool signSuccess;
        SignatureData sigdata;

        CCoins coins;
        if (!(view.GetCoins(wtx.vin[i].prevout.hash, coins) && coins.IsAvailable(wtx.vin[i].prevout.n)))
        {
            break;
        }

        CAmount value = coins.vout[wtx.vin[i].prevout.n].nValue;

        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &wtx, i, value, coins.vout[wtx.vin[i].prevout.n].scriptPubKey), coins.vout[wtx.vin[i].prevout.n].scriptPubKey, sigdata, CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()));

        if (!signSuccess)
        {
            LogPrintf("%s: failure to sign identity update tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            printf("%s: failure to sign identity update tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, i, sigdata);
        }
    }
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    if (returnTx)
    {
        return EncodeHexTx(wtx);
    }
    else if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }
    return wtx.GetHash().GetHex();
}

UniValue revokeidentity(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "revokeidentity \"nameorID\" (returntx)\n"
            "\n\n"

            "\nArguments\n"
            "       \"returntx\"                        (bool,   optional) defaults to false and transaction is sent, if true, transaction is signed by this wallet and returned\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("revokeidentity", "\"nameorID\"")
            + HelpExampleRpc("revokeidentity", "\"nameorID\"")
        );
    }

    CheckIdentityAPIsValid();

    // get identity
    bool returnTx = false;
    CTxDestination idDest = DecodeDestination(uni_get_str(params[0]));

    if (idDest.which() != COptCCParams::ADDRTYPE_ID)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid JSON ID parameter");
    }

    CIdentityID idID(GetDestinationID(idDest));

    if (params.size() > 1)
    {
        returnTx = uni_get_bool(params[1], false);
    }

    CTxIn idTxIn;
    CIdentity oldID;
    uint32_t idHeight;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!(oldID = CIdentity::LookupIdentity(idID, 0, &idHeight, &idTxIn)).IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ID not found " + EncodeDestination(idID));
    }

    CIdentity newID(oldID);
    newID.Revoke();

    // create the identity definition transaction
    std::vector<CRecipient> outputs = std::vector<CRecipient>({{newID.IdentityUpdateOutputScript(), 0, false}});
    CWalletTx wtx;

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason, nullptr, false))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Unable to create update transaction: " + failReason);
    }
    CMutableTransaction mtx(wtx);

    // add the spend of the last ID transaction output
    mtx.vin.push_back(idTxIn);

    // all of the reservation output is actually the fee offer, so zero the output
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    // now sign
    CCoinsViewCache view(pcoinsTip);
    for (int i = 0; i < wtx.vin.size(); i++)
    {
        bool signSuccess;
        SignatureData sigdata;

        CCoins coins;
        if (!(view.GetCoins(wtx.vin[i].prevout.hash, coins) && coins.IsAvailable(wtx.vin[i].prevout.n)))
        {
            break;
        }

        CAmount value = coins.vout[wtx.vin[i].prevout.n].nValue;

        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &wtx, i, value, coins.vout[wtx.vin[i].prevout.n].scriptPubKey), coins.vout[wtx.vin[i].prevout.n].scriptPubKey, sigdata, CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()));

        if (!signSuccess)
        {
            LogPrintf("%s: failure to sign identity revocation tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            printf("%s: failure to sign identity revocation tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, i, sigdata);
        }
    }
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    if (returnTx)
    {
        return EncodeHexTx(wtx);
    }
    else if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }
    return wtx.GetHash().GetHex();
}

UniValue recoveridentity(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "recoveridentity \"jsonidentity\" (returntx)\n"
            "\n\n"

            "\nArguments\n"
            "       \"returntx\"                        (bool,   optional) defaults to false and transaction is sent, if true, transaction is signed by this wallet and returned\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("recoveridentity", "\'{\"name\" : \"myname\"}\'")
            + HelpExampleRpc("recoveridentity", "\'{\"name\" : \"myname\"}\'")
        );
    }
    CheckIdentityAPIsValid();

    // get identity
    bool returnTx = false;
    CIdentity newID(params[0]);

    if (!newID.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid JSON ID parameter");
    }

    if (params.size() > 1)
    {
        returnTx = uni_get_bool(params[1], false);
    }

    CTxIn idTxIn;
    CIdentity oldID;
    uint32_t idHeight;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!(oldID = CIdentity::LookupIdentity(newID.GetID(), 0, &idHeight, &idTxIn)).IsValid())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ID not found " + newID.ToUniValue().write());
    }

    if (!oldID.IsRevoked())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Identity must be revoked in order to recover : " + newID.name);
    }

    // create the identity definition transaction
    std::vector<CRecipient> outputs = std::vector<CRecipient>({{newID.IdentityUpdateOutputScript(), 0, false}});
    CWalletTx wtx;

    CReserveKey reserveKey(pwalletMain);
    CAmount fee;
    int nChangePos;
    string failReason;

    if (!pwalletMain->CreateTransaction(outputs, wtx, reserveKey, fee, nChangePos, failReason, nullptr, false))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Unable to create update transaction: " + failReason);
    }
    CMutableTransaction mtx(wtx);

    // add the spend of the last ID transaction output
    mtx.vin.push_back(idTxIn);

    // all of the reservation output is actually the fee offer, so zero the output
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    // now sign
    CCoinsViewCache view(pcoinsTip);
    for (int i = 0; i < wtx.vin.size(); i++)
    {
        bool signSuccess;
        SignatureData sigdata;

        CCoins coins;
        if (!(view.GetCoins(wtx.vin[i].prevout.hash, coins) && coins.IsAvailable(wtx.vin[i].prevout.n)))
        {
            break;
        }

        CAmount value = coins.vout[wtx.vin[i].prevout.n].nValue;

        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &wtx, i, value, coins.vout[wtx.vin[i].prevout.n].scriptPubKey), coins.vout[wtx.vin[i].prevout.n].scriptPubKey, sigdata, CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()));

        if (!signSuccess)
        {
            LogPrintf("%s: failure to sign identity recovery tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            printf("%s: failure to sign identity recovery tx for input %d from output %d of %s\n", __func__, i, wtx.vin[i].prevout.n, wtx.vin[i].prevout.hash.GetHex().c_str());
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, i, sigdata);
        }
    }
    *static_cast<CTransaction*>(&wtx) = CTransaction(mtx);

    if (returnTx)
    {
        return EncodeHexTx(wtx);
    }
    else if (!pwalletMain->CommitTransaction(wtx, reserveKey))
    {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Could not commit transaction " + wtx.GetHash().GetHex());
    }
    return wtx.GetHash().GetHex();
}

UniValue getidentity(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "getidentity \"name\"\n"
            "\n\n"

            "\nArguments\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("getidentity", "\"name@\"")
            + HelpExampleRpc("getidentity", "\"name@\"")
        );
    }

    CheckIdentityAPIsValid();

    CTxDestination idID = DecodeDestination(uni_get_str(params[0]));
    if (idID.which() != COptCCParams::ADDRTYPE_ID)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Identity parameter must be valid friendly name or identity address: \"" + uni_get_str(params[0]) + "\"");
    }

    CTxIn idTxIn;
    uint32_t height;

    CIdentity identity;
    bool canSign = false, canSpend = false, found = false;

    if (pwalletMain)
    {
        LOCK(pwalletMain->cs_wallet);
        uint256 txID;
        std::pair<CIdentityMapKey, CIdentityMapValue> keyAndIdentity;
        if (pwalletMain->GetIdentity(GetDestinationID(idID), keyAndIdentity))
        {
            found = true;
            canSign = keyAndIdentity.first.flags & keyAndIdentity.first.CAN_SIGN;
            canSpend = keyAndIdentity.first.flags & keyAndIdentity.first.CAN_SPEND;
            identity = static_cast<CIdentity>(keyAndIdentity.second);
        }
    }

    identity = CIdentity::LookupIdentity(CIdentityID(GetDestinationID(idID)), 0, &height, &idTxIn);

    UniValue ret(UniValue::VOBJ);

    uint160 parent;
    if (identity.IsValid() && identity.name == CleanName(identity.name, parent, true))
    {
        ret.push_back(Pair("identity", identity.ToUniValue()));
        ret.push_back(Pair("status", identity.IsRevoked() ? "revoked" : "active"));
        ret.push_back(Pair("canspendfor", canSpend));
        ret.push_back(Pair("cansignfor", canSign));
        ret.push_back(Pair("blockheight", (int64_t)height));
        ret.push_back(Pair("txid", idTxIn.prevout.hash.GetHex()));
        ret.push_back(Pair("vout", (int32_t)idTxIn.prevout.n));
        return ret;
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Identity not found");
    }
}

UniValue IdentityPairToUni(const std::pair<CIdentityMapKey, CIdentityMapValue *> &identity)
{
    UniValue oneID(UniValue::VOBJ);

    if (identity.first.IsValid() && identity.second->IsValid())
    {
        oneID.push_back(Pair("identity", identity.second->ToUniValue()));
        oneID.push_back(Pair("blockheight", (int64_t)identity.first.blockHeight));
        oneID.push_back(Pair("txid", identity.second->txid.GetHex()));
        if (identity.second->IsRevoked())
        {
            oneID.push_back(Pair("status", "revoked"));
            oneID.push_back(Pair("canspendfor", 0));
            oneID.push_back(Pair("cansignfor", 0));
        }
        else
        {
            oneID.push_back(Pair("status", "active"));
            oneID.push_back(Pair("canspendfor", bool(identity.first.flags & identity.first.CAN_SPEND)));
            oneID.push_back(Pair("cansignfor", bool(identity.first.flags & identity.first.CAN_SIGN)));
        }
    }
    return oneID;
}

UniValue listidentities(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
    {
        throw runtime_error(
            "listidentities (includecansign) (includewatchonly)\n"
            "\n\n"

            "\nArguments\n"
            "    \"includecanspend\"    (bool, optional, default=true)    Include identities for which we can spend/authorize\n"
            "    \"includecansign\"     (bool, optional, default=true)    Include identities that we can only sign for but not spend\n"
            "    \"includewatchonly\"   (bool, optional, default=false)   Include identities that we can neither sign nor spend, but are either watched or are co-signers with us\n"

            "\nResult:\n"

            "\nExamples:\n"
            + HelpExampleCli("listidentities", "\'{\"name\" : \"myname\"}\'")
            + HelpExampleRpc("listidentities", "\'{\"name\" : \"myname\"}\'")
        );
    }

    CheckIdentityAPIsValid();

    std::vector<std::pair<CIdentityMapKey, CIdentityMapValue *>> mine, imsigner, notmine;

    bool includeCanSpend = params.size() > 0 ? uni_get_bool(params[0], true) : true;
    bool includeCanSign = params.size() > 1 ? uni_get_bool(params[1], true) : true;
    bool includeWatchOnly = params.size() > 2 ? uni_get_bool(params[2], false) : false;

    if (pwalletMain->GetIdentities(mine, imsigner, notmine))
    {
        UniValue ret(UniValue::VARR);
        if (includeCanSpend)
        {
            for (auto identity : mine)
            {
                uint160 parent;
                if (identity.second->IsValid() && identity.second->name == CleanName(identity.second->name, parent, true))
                {
                    ret.push_back(IdentityPairToUni(identity));
                }
            }
        }
        if (includeCanSign)
        {
            for (auto identity : imsigner)
            {
                uint160 parent;
                if (identity.second->IsValid() && identity.second->name == CleanName(identity.second->name, parent, true))
                {
                    ret.push_back(IdentityPairToUni(identity));
                }
            }
        }
        if (includeWatchOnly)
        {
            for (auto identity : notmine)
            {
                uint160 parent;
                if (identity.second->IsValid() && identity.second->name == CleanName(identity.second->name, parent, true))
                {
                    ret.push_back(IdentityPairToUni(identity));
                }
            }
        }
        return ret;
    }
    else
    {
        return NullUniValue;
    }
}

UniValue addmergedblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 5)
    {
        throw runtime_error(
            "addmergedblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAdds a fully prepared block and its header to the current merge mining queue of this daemon.\n"
            "Parameters determine the action to take if adding this block would exceed the available merge mining slots.\n"
            "Default action to take if adding would exceed available space is to replace the choice with the least ROI if this block provides more.\n"

            "\nArguments\n"
            "1. \"hexdata\"                     (string, required) the hex-encoded, complete, unsolved block data to add. nTime, and nSolution are replaced.\n"
            "2. \"name\"                        (string, required) chain name symbol\n"
            "3. \"rpchost\"                     (string, required) host address for RPC connection\n"
            "4. \"rpcport\"                     (int,    required) port address for RPC connection\n"
            "5. \"userpass\"                    (string, required) credentials for login to RPC\n"

            "\nResult:\n"
            "\"deserialize-invalid\" - block could not be deserialized and was rejected as invalid\n"
            "\"blocksfull\"          - block did not exceed others in estimated ROI, and there was no room for an additional merge mined block\n"

            "\nExamples:\n"
            + HelpExampleCli("addmergedblock", "\"hexdata\" \'{\"chainid\" : \"hexstring\", \"rpchost\" : \"127.0.0.1\", \"rpcport\" : portnum}\'")
            + HelpExampleRpc("addmergedblock", "\"hexdata\" \'{\"chainid\" : \"hexstring\", \"rpchost\" : \"127.0.0.1\", \"rpcport\" : portnum, \"estimatedroi\" : (verusreward/hashrate)}\'")
        );
    }

    CheckPBaaSAPIsValid();

    // check to see if we should replace any existing block or add a new one. if so, add this to the merge mine vector
    string name = params[1].get_str();
    if (name == "")
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "must provide chain name to merge mine");
    }

    string rpchost = params[2].get_str();
    int32_t rpcport = params[3].get_int();
    string rpcuserpass = params[4].get_str();

    if (rpchost == "" || rpcport == 0 || rpcuserpass == "")
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "must provide valid RPC connection parameters to merge mine");
    }

    uint160 chainID = CCrossChainRPCData::GetChainID(name);

    // confirm data from blockchain
    CRPCChainData chainData;
    CPBaaSChainDefinition chainDef;
    if (ConnectedChains.GetChainInfo(chainID, chainData))
    {
        chainDef = chainData.chainDefinition;
    }

    if (!chainDef.IsValid() && !GetChainDefinition(name, chainDef))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "chain not found");
    }

    CBlock blk;

    if (!DecodeHexBlk(blk, params[0].get_str()))
        return "deserialize-invalid";

    CPBaaSMergeMinedChainData blkData = CPBaaSMergeMinedChainData(chainDef, rpchost, rpcport, rpcuserpass, blk);

    return ConnectedChains.AddMergedBlock(blkData) ? NullUniValue : "blocksfull";
}

UniValue submitmergedblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "submitmergedblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit one more more new blocks to one or more networks.\n"
            "Each merged block submission may be valid for Verus and/or up to 8 merge mined chains.\n"
            "The submitted block consists of a valid block for this chain, along with embedded headers of up to 8 other chains.\n"
            "If the hash for this header meets targets of other chains that have been added with 'addmergedblock', this API will\n"
            "submit those blocks to the specified URL endpoints with an RPC 'submitblock' request."
            "\nAttempts to submit one more more new blocks to one or more networks.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
            "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"
            "\nResult:\n"
            "\"duplicate\" - node already has valid copy of block\n"
            "\"duplicate-invalid\" - node already has block, but it is invalid\n"
            "\"duplicate-inconclusive\" - node already has block but has not validated it\n"
            "\"inconclusive\" - node has not validated the block, it may not be on the node's current best chain\n"
            "\"rejected\" - block was rejected as invalid\n"
            "For more information on submitblock parameters and results, see: https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki#block-submission\n"
            "\nExamples:\n"
            + HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
        );

    CheckPBaaSAPIsValid();

    CBlock block;
    //LogPrintStr("Hex block submission: " + params[0].get_str());
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex *pindex = mi->second;
            if (pindex)
            {
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                // Otherwise, we might only have the header - process the block before returning
                fBlockPresent = true;
            }
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    //printf("submitblock, height=%d, coinbase sequence: %d, scriptSig: %s\n", chainActive.LastTip()->GetHeight()+1, block.vtx[0].vin[0].nSequence, block.vtx[0].vin[0].scriptSig.ToString().c_str());
    bool fAccepted = ProcessNewBlock(1, chainActive.LastTip()->GetHeight()+1, state, Params(), NULL, &block, true, NULL);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent)
    {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted)
    {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue getmergedblocktemplate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getblocktemplate ( \"jsonrequestobject\" )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments:\n"
            "1. \"jsonrequestobject\"       (string, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\" or omitted\n"
            "       \"capabilities\":[       (array, optional) A list of strings\n"
            "           \"support\"           (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "         ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                     (numeric) The block version\n"
            "  \"previousblockhash\" : \"xxxx\",    (string) The hash of current highest block\n"
            "  \"finalsaplingroothash\" : \"xxxx\", (string) The hash of the final sapling root\n"
            "  \"transactions\" : [                 (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",          (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"hash\" : \"xxxx\",          (string) hash/id encoded in little-endian hexadecimal\n"
            "         \"depends\" : [              (array) array of numbers \n"
            "             n                        (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                   (numeric) difference in value between transaction inputs and outputs (in Satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,               (numeric) total number of SigOps, as counted for purposes of block limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any\n"
            "         \"required\" : true|false     (boolean) if provided and true, this transaction must be in the final block\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
//            "  \"coinbaseaux\" : {                  (json object) data that should be included in the coinbase's scriptSig content\n"
//            "      \"flags\" : \"flags\"            (string) \n"
//            "  },\n"
//            "  \"coinbasevalue\" : n,               (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in Satoshis)\n"
            "  \"coinbasetxn\" : { ... },           (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",               (string) The hash target\n"
            "  \"mintime\" : xxx,                   (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                      (array of string) list of ways the block template may be changed \n"
            "     \"value\"                         (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",   (string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxx\",                 (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getblocktemplate", "")
            + HelpExampleRpc("getblocktemplate", "")
         );

    CheckPBaaSAPIsValid();

    LOCK(cs_main);

    // Wallet or miner address is required because we support coinbasetxn
    if (GetArg("-mineraddress", "").empty()) {
#ifdef ENABLE_WALLET
        if (!pwalletMain) {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Wallet disabled and -mineraddress not set");
        }
#else
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "verusd compiled without wallet and -mineraddress not set");
#endif
    }

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    // TODO: Re-enable coinbasevalue once a specification has been written
    bool coinbasetxn = true;
    if (params.size() > 0)
    {
        const UniValue& oparam = params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end()) {
                CBlockIndex *pindex = mi->second;
                if (pindex)
                {
                    if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                        return "duplicate";
                    if (pindex->nStatus & BLOCK_FAILED_MASK)
                        return "duplicate-invalid";
                }
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.LastTip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    bool fvNodesEmpty;
    {
        LOCK(cs_vNodes);
        fvNodesEmpty = vNodes.empty();
    }
    if (Params().MiningRequiresPeers() && (IsNotInSync() || fvNodesEmpty))
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Cannot get a block template while no peers are connected or chain not in sync!");
    }

    //if (IsInitialBlockDownload())
     //   throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Zcash is downloading blocks...");

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        boost::system_time checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.LastTip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = boost::get_system_time() + boost::posix_time::minutes(1);

            boost::unique_lock<boost::mutex> lock(csBestBlock);
            while (chainActive.LastTip()->GetBlockHash() == hashWatchedChain && IsRPCRunning())
            {
                if (!cvBlockChange.timed_wait(lock, checktxtime))
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += boost::posix_time::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlockTemplate* pblocktemplate;
    if (pindexPrev != chainActive.LastTip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = NULL;

        // Store the pindexBest used before CreateNewBlockWithKey, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.LastTip();
        nStart = GetTime();

        // Create new block
        if(pblocktemplate)
        {
            delete pblocktemplate;
            pblocktemplate = NULL;
        }
#ifdef ENABLE_WALLET
        CReserveKey reservekey(pwalletMain);
        pblocktemplate = CreateNewBlockWithKey(reservekey,chainActive.LastTip()->GetHeight()+1,KOMODO_MAXGPUCOUNT);
#else
        pblocktemplate = CreateNewBlockWithKey();
#endif
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory or no available utxo for staking");

        // Need to update only after we know CreateNewBlockWithKey succeeded
        pindexPrev = pindexPrevNew;
    }
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience

    // Update nTime
    UpdateTime(pblock, Params().GetConsensus(), pindexPrev);
    pblock->nNonce = uint256();

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue txCoinbase = NullUniValue;
    UniValue transactions(UniValue::VARR);
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    BOOST_FOREACH (const CTransaction& tx, pblock->vtx) {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() && !coinbasetxn)
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.push_back(Pair("data", EncodeHexTx(tx)));

        entry.push_back(Pair("hash", txHash.GetHex()));

        UniValue deps(UniValue::VARR);
        BOOST_FOREACH (const CTxIn &in, tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.push_back(Pair("depends", deps));

        int index_in_template = i - 1;
        entry.push_back(Pair("fee", pblocktemplate->vTxFees[index_in_template]));
        entry.push_back(Pair("sigops", pblocktemplate->vTxSigOps[index_in_template]));

        if (tx.IsCoinBase()) {
            // Show founders' reward if it is required
            //if (pblock->vtx[0].vout.size() > 1) {
                // Correct this if GetBlockTemplate changes the order
            //    entry.push_back(Pair("foundersreward", (int64_t)tx.vout[1].nValue));
            //}
            CAmount nReward = GetBlockSubsidy(chainActive.LastTip()->GetHeight()+1, Params().GetConsensus());
            entry.push_back(Pair("coinbasevalue", nReward));
            entry.push_back(Pair("required", true));
            txCoinbase = entry;
        } else
            transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    static UniValue aMutable(UniValue::VARR);
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("capabilities", aCaps));
    result.push_back(Pair("version", pblock->nVersion));
    result.push_back(Pair("previousblockhash", pblock->hashPrevBlock.GetHex()));
    result.push_back(Pair("finalsaplingroothash", pblock->hashFinalSaplingRoot.GetHex()));
    result.push_back(Pair("transactions", transactions));
    if (coinbasetxn) {
        assert(txCoinbase.isObject());
        result.push_back(Pair("coinbasetxn", txCoinbase));
    } else {
        result.push_back(Pair("coinbaseaux", aux));
        result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue));
    }
    result.push_back(Pair("longpollid", chainActive.LastTip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast)));
    if ( ASSETCHAINS_STAKED != 0 )
    {
        arith_uint256 POWtarget; int32_t PoSperc;
        POWtarget = komodo_PoWtarget(&PoSperc,hashTarget,(int32_t)(pindexPrev->GetHeight()+1),ASSETCHAINS_STAKED);
        result.push_back(Pair("target", POWtarget.GetHex()));
        result.push_back(Pair("PoSperc", (int64_t)PoSperc));
        result.push_back(Pair("ac_staked", (int64_t)ASSETCHAINS_STAKED));
        result.push_back(Pair("origtarget", hashTarget.GetHex()));
    } else result.push_back(Pair("target", hashTarget.GetHex()));
    result.push_back(Pair("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1));
    result.push_back(Pair("mutable", aMutable));
    result.push_back(Pair("noncerange", "00000000ffffffff"));
    result.push_back(Pair("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS));
    result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_SIZE));
    result.push_back(Pair("curtime", pblock->GetBlockTime()));
    result.push_back(Pair("bits", strprintf("%08x", pblock->nBits)));
    result.push_back(Pair("height", (int64_t)(pindexPrev->GetHeight()+1)));

    //fprintf(stderr,"return complete template\n");
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "identity",     "registernamecommitment",       &registernamecommitment, true  },
    { "identity",     "registeridentity",             &registeridentity,       true  },
    { "identity",     "updateidentity",               &updateidentity,         true  },
    { "identity",     "revokeidentity",               &revokeidentity,         true  },
    { "identity",     "recoveridentity",              &recoveridentity,        true  },
    { "identity",     "getidentity",                  &getidentity,            true  },
    { "identity",     "listidentities",               &listidentities,         true  },
    { "multichain",   "definechain",                  &definechain,            true  },
    { "multichain",   "getdefinedchains",             &getdefinedchains,       true  },
    { "multichain",   "getchaindefinition",           &getchaindefinition,     true  },
    { "multichain",   "getnotarizationdata",          &getnotarizationdata,    true  },
    { "multichain",   "getcrossnotarization",         &getcrossnotarization,   true  },
    { "multichain",   "submitacceptednotarization",   &submitacceptednotarization, true },
    { "multichain",   "paynotarizationrewards",       &paynotarizationrewards, true  },
    { "multichain",   "getinitialcurrencystate",      &getinitialcurrencystate, true  },
    { "multichain",   "getcurrencystate",             &getcurrencystate,       true  },
    { "multichain",   "sendreserve",                  &sendreserve,            true  },
    { "multichain",   "getpendingchaintransfers",     &getpendingchaintransfers, true  },
    { "multichain",   "getchainexports",              &getchainexports,        true  },
    { "multichain",   "getchainimports",              &getchainimports,        true  },
    { "multichain",   "reserveexchange",              &reserveexchange,        true  },
    { "multichain",   "getlatestimportsout",          &getlatestimportsout,    true  },
    { "multichain",   "getlastimportin",              &getlastimportin,        true  },
    { "multichain",   "refundfailedlaunch",           &refundfailedlaunch,     true  },
    { "multichain",   "refundfailedlaunch",           &refundfailedlaunch,     true  },
    { "multichain",   "getmergedblocktemplate",       &getmergedblocktemplate, true  },
    { "multichain",   "addmergedblock",               &addmergedblock,         true  }
};

void RegisterPBaaSRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
