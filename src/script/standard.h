// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_SCRIPT_STANDARD_H
#define BITCOIN_SCRIPT_STANDARD_H

#include "uint256.h"
#include "interpreter.h"
#include "key.h"

#include <boost/variant.hpp>

#include <stdint.h>

static const unsigned int MAX_OP_RETURN_RELAY = MAX_SCRIPT_SIZE;      //! bytes
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added.
 *
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

/**
 * Standard script verification flags that standard transactions will comply
 * with. However scripts violating these flags may still be present in valid
 * blocks and we must accept those blocks.
 */
static const unsigned int STANDARD_SCRIPT_VERIFY_FLAGS = MANDATORY_SCRIPT_VERIFY_FLAGS |
                                                         // SCRIPT_VERIFY_DERSIG is always enforced
                                                         SCRIPT_VERIFY_STRICTENC |
                                                         SCRIPT_VERIFY_MINIMALDATA |
                                                         SCRIPT_VERIFY_NULLDUMMY |
                                                         SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS |
                                                         SCRIPT_VERIFY_CLEANSTACK |
                                                         SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY |
                                                         SCRIPT_VERIFY_LOW_S;

/** For convenience, standard but not mandatory verify flags. */
static const unsigned int STANDARD_NOT_MANDATORY_VERIFY_FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS & ~MANDATORY_SCRIPT_VERIFY_FLAGS;

enum txnouttype
{
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_CRYPTOCONDITION,
    TX_NULL_DATA,
};

class CStakeParams
{
    public:
        static const uint32_t STAKE_MINPARAMS = 4;
        static const uint32_t STAKE_MAXPARAMS = 5;
        
        uint32_t srcHeight;
        uint32_t blkHeight;
        uint256 prevHash;
        CPubKey pk;
    
        CStakeParams() : srcHeight(0), blkHeight(0), prevHash(), pk() {}

        CStakeParams(const std::vector<std::vector<unsigned char>> &vData);

        CStakeParams(uint32_t _srcHeight, uint32_t _blkHeight, const uint256 &_prevHash, const CPubKey &_pk) :
            srcHeight(_srcHeight), blkHeight(_blkHeight), prevHash(_prevHash), pk(_pk) {}

        std::vector<unsigned char> AsVector()
        {
            std::vector<unsigned char> ret;
            CScript scr = CScript();
            scr << OPRETTYPE_STAKEPARAMS;
            scr << srcHeight;
            scr << blkHeight;
            scr << std::vector<unsigned char>(prevHash.begin(), prevHash.end());
            
            if (pk.IsValid())
            {
                scr << std::vector<unsigned char>(pk.begin(), pk.end());
            }
                                    
            ret = std::vector<unsigned char>(scr.begin(), scr.end());
            return ret;
        }

        bool IsValid() const { return srcHeight != 0; }
};

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest);
bool IsTransparentAddress(const CTxDestination& dest);

const char* GetTxnOutputType(txnouttype t);

bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);
int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions);
bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet, bool returnPubKey=false);
bool ExtractDestinations(const CScript& scriptPubKey, 
                         txnouttype& typeRet, 
                         std::vector<CTxDestination>& addressRet, 
                         int &nRequiredRet, 
                         const CKeyStore *pKeyStore=nullptr, 
                         bool *canSign=nullptr, 
                         bool *canSpend=nullptr,
                         uint32_t lastIdHeight=INT_MAX,
                         std::map<uint160, CKey> *pPrivKeys=nullptr);

CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

bool IsPayToCryptoCondition(const CScript &scr, COptCCParams &ccParams);

template <typename T>
bool IsPayToCryptoCondition(const CScript &scr, COptCCParams &ccParams, T &extraObject)
{
    CScript subScript;
    std::vector<std::vector<unsigned char>> vParams;
    COptCCParams p;

    if (scr.IsPayToCryptoCondition(&subScript, vParams))
    {
        if (!vParams.empty())
        {
            ccParams = COptCCParams(vParams[0]);
            if (ccParams.IsValid() && ccParams.vData.size() > 0)
            {
                try
                {
                    extraObject = T(ccParams.vData[0]);
                }
                catch(const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
        }
        return true;
    }
    return false;
}

CTxDestination DestFromAddressHash(int scriptType, uint160& addressHash);
CScript::ScriptType AddressTypeFromDest(const CTxDestination &dest);

#endif // BITCOIN_SCRIPT_STANDARD_H
