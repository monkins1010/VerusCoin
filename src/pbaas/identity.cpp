/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides support for PBaaS identity definition,
 * 
 * This is a decentralized identity class that provides the minimum
 * basic function needed to enable persistent DID-similar identities, 
 * needed for signatories, that will eventually bridge compatibly to
 * DID identities.
 * 
 * 
 */
#include "pbaas/identity.h"
#include "script/cc.h"
#include "cc/CCinclude.h"

extern CTxMemPool mempool;

CPrincipal::CPrincipal(const UniValue &uni)
{
    nVersion = uni_get_int(find_value(uni, "version"));
    flags = uni_get_int(find_value(uni, "flags"));
    UniValue primaryAddressesUni = find_value(uni, "primaryAddresses");
    if (primaryAddressesUni.isArray())
    {
        for (int i = 0; i < primaryAddressesUni.size(); i++)
        {
            try
            {
                primaryAddresses.push_back(DecodeDestination(uni_get_str(primaryAddressesUni[i])));
            }
            catch (const std::exception &e)
            {
                printf("%s: bad address %s\n", __func__, primaryAddressesUni[i].write().c_str());
                LogPrintf("%s: bad address %s\n", __func__, primaryAddressesUni[i].write().c_str());
                nVersion = VERSION_INVALID;
            }
        }
    }

    minSigs = uni_get_int(find_value(uni, "minimumtransparent"));
}

UniValue CPrincipal::ToUniValue() const
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("version", (int32_t)nVersion));
    obj.push_back(Pair("flags", (int32_t)flags));

    UniValue primaryAddressesUni(UniValue::VARR);
    for (int i = 0; i < primaryAddresses.size(); i++)
    {
        primaryAddressesUni.push_back(EncodeDestination(primaryAddresses[i]));
    }
    obj.push_back(Pair("primaryAddresses", primaryAddressesUni));
    obj.push_back(Pair("minimumtransparent", minSigs));
    return obj;
}

CIdentity::CIdentity(const UniValue &uni) : CPrincipal(uni)
{
    name = uni_get_int(find_value(uni, "name"));
    UniValue namesUni = find_value(uni, "names");
    if (namesUni.isArray())
    {
        for (int i = 0; i < namesUni.size(); i++)
        {
            try
            {
                names.push_back(uni_get_str(namesUni[i]));
            }
            catch (const std::exception &e)
            {
                printf("%s: name is not string %s\n", __func__, namesUni[i].write().c_str());
                LogPrintf("%s: name is not string %s\n", __func__, namesUni[i].write().c_str());
                nVersion = VERSION_INVALID;
            }
        }
    }
    revocationAuthority = uint160(GetDestinationID(DecodeDestination(uni_get_str(uni, "revocationauthorityid"))));
    recoveryAuthority = uint160(GetDestinationID(DecodeDestination(uni_get_str(uni, "recoveryauthorityid"))));
    libzcash::PaymentAddress pa = DecodePaymentAddress(uni_get_str(uni, "privateaddress"));

    if (revocationAuthority.IsNull() || recoveryAuthority.IsNull() || boost::get<libzcash::SaplingPaymentAddress>(&pa) == nullptr)
    {
        printf("%s: invalid Sapling address %s\n", __func__, uni_get_str(uni, "privateaddress").c_str());
        LogPrintf("%s: invalid Sapling address %s\n", __func__, uni_get_str(uni, "privateaddress").c_str());
        nVersion = VERSION_INVALID;
    }
    else
    {
        privateAddress = *boost::get<libzcash::SaplingPaymentAddress>(&pa);
    }
}

UniValue CIdentity::ToUniValue() const
{
    UniValue obj = ((CPrincipal *)this)->ToUniValue();

    obj.push_back(Pair("name", name));

    UniValue aliases(UniValue::VARR);
    for (int i = 0; i < names.size(); i++)
    {
        aliases.push_back(names[i]);
    }
    obj.push_back(Pair("names", aliases));

    obj.push_back(Pair("revocationauthorityid", EncodeDestination(CTxDestination(CKeyID(revocationAuthority)))));
    obj.push_back(Pair("recoveryauthorityid", EncodeDestination(CTxDestination(CKeyID(recoveryAuthority)))));
    obj.push_back(Pair("privateaddress", EncodePaymentAddress(privateAddress)));
    return obj;
}

CIdentity::CIdentity(const CScript &scriptPubKey)
{
    COptCCParams p;
    if (IsPayToCryptoCondition(scriptPubKey, p) && p.IsValid() && p.evalCode == EVAL_IDENTITY_PRIMARY && p.vData.size())
    {
        FromVector(p.vData[0], *this);

        // TODO - remove this after validation is finished, check now in case some larger strings got into the chain
        name = std::string(name, 0, (KOMODO_ASSETCHAIN_MAXLEN - 1));
        string invalidChars = "\\/:?\"<>|";
        for (int i = 0; i < name.size(); i++)
        {
            if (invalidChars.find(name[i]) != string::npos)
            {
                name[i] = '_';
            }
        }
    }
}

CIdentity::CIdentity(const CTransaction &tx)
{
    std::set<uint160> ids;
    int idCount;

    nVersion = PBAAS_VERSION_INVALID;
    for (auto out : tx.vout)
    {
        *this = CIdentity(out.scriptPubKey);
        if (IsValid())
        {
            break;
        }
    }
}

uint160 CIdentity::GetNameID(const std::string &name) const
{
    const char *idName = name.c_str();
    uint256 idHash;
    if (parent.IsNull())
    {
        idHash = Hash(idName, idName + strlen(idName));
    }
    else
    {
        idHash = Hash(idName, idName + strlen(idName));
        idHash = Hash(parent.begin(), parent.end(), idHash.begin(), idHash.end());

    }
    return Hash160(idHash.begin(), idHash.end());
}

uint160 CIdentity::GetNameID() const
{
    return GetNameID(name);
}

CIdentity CIdentity::LookupIdentity(const uint160 &nameID)
{
    CIdentity ret;

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    LOCK2(cs_main, mempool.cs);

    CKeyID keyID(CCrossChainRPCData::GetConditionID(nameID, EVAL_IDENTITY_PRIMARY));

    if (GetAddressUnspent(keyID, 1, unspentOutputs))
    {
        CCoinsViewCache view(pcoinsTip);

        for (auto it = unspentOutputs.begin(); !ret.IsValid() && it != unspentOutputs.end(); it++)
        {
            CCoins coins;

            if (view.GetCoins(it->first.txhash, coins))
            {
                if (coins.IsAvailable(it->first.index))
                {
                    // check the mempool for spent/modified
                    CSpentIndexKey key(it->first.txhash, it->first.index);
                    CSpentIndexValue value;
                    if (mempool.getSpentIndex(key, value))
                    {
                        do
                        {
                            CTransaction tx;
                            if (mempool.lookup(it->first.txhash, tx))
                            {
                                for (int i = 0; !ret.IsValid() && i < tx.vout.size(); i++)
                                {
                                    ret = CIdentity(tx.vout[i].scriptPubKey);
                                    key = CSpentIndexKey(tx.GetHash(), i);
                                }
                            }
                            else
                            {
                                key = CSpentIndexKey(uint256(), -1);
                            }
                        } while (mempool.getSpentIndex(key, value));
                    }
                    else
                    {
                        ret = CIdentity(coins.vout[it->first.index].scriptPubKey);
                    }
                }
            }
            else
            {
                printf("%s: cannot retrieve transaction %s\n", __func__, it->first.txhash.GetHex().c_str());
            }
        }
    }
    return ret;
}

CIdentity CIdentity::LookupIdentity(const std::string &name)
{
    return LookupIdentity(GetNameID(name));
}

// TODO:PBAAS this type of output must be recognized and supported in the CC signature code
CScript CIdentity::IdentityUpdateOutputScript() const
{
    CScript ret;

    if (!IsValid())
    {
        return ret;
    }

    // output crypto condition can be updated according to different eval restrictions by
    // primary principal, revocation authority, and recovery authority

    // if revocation authority or recovery authority are revoked, they are not included as possible spenders
    std::vector<CC*> multisigs;

    multisigs.push_back(MakeCCcondMofN(EVAL_IDENTITY_PRIMARY, primaryAddresses, minSigs));

    uint160 primaryID = GetNameID();
    CIdentity revocation = (primaryID == revocationAuthority) ? *this : LookupIdentity(revocationAuthority);
    if (!revocation.IsValidUnrevoked())
    {
        revocation = *this;
    }
    CIdentity recovery = (primaryID == recoveryAuthority) ? *this : LookupIdentity(recoveryAuthority);
    if (!recovery.IsValidUnrevoked())
    {
        recovery = *this;
    }

    multisigs.push_back(MakeCCcondMofN(EVAL_IDENTITY_REVOKE, revocation.primaryAddresses, revocation.minSigs));
    multisigs.push_back(MakeCCcondMofN(EVAL_IDENTITY_RECOVER, recovery.primaryAddresses, recovery.minSigs));

    CC *compoundMultisig = CCNewThreshold(1, multisigs);

    ret = CCPubKey(compoundMultisig);
    cc_free(compoundMultisig);

    // we will encode the compound multisig's primary identity and revocation + recovery principals in the output
    // that will allow us to reconstruct the compount CC when signing
    // TODO:PBAAS this CC should actually store only the primary ID as destination and bind during signing and validation to the
    // latest state of the identity
    std::vector<std::vector<unsigned char>> vvch({::AsVector(*this), ::AsVector((CPrincipal)revocation), ::AsVector((CPrincipal)recovery)});

    std::vector<CTxDestination> dests;
    dests.push_back(CTxDestination(CKeyID(CCrossChainRPCData::GetConditionID(GetNameID(), EVAL_IDENTITY_PRIMARY))));

    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V3, EVAL_IDENTITY_PRIMARY, 1, (uint8_t)(dests.size()), dests, vvch);

    // add the object to the end of the script
    ret << vParams.AsVector() << OP_DROP;
    return ret;
}

bool ValidateIdentityPrimary(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    // validate that from the input to the output, the only changes are those allowed by the primary identity
    return true;
}

bool ValidateIdentityRevoke(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    // validate that from the input to the output, the only changes are those allowed by the primary identity
    return true;
}

bool ValidateIdentityRecover(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    // validate that from the input to the output, the only changes are those allowed by the primary identity
    return true;
}

bool IsIdentityInput(const CScript &scriptSig)
{
    return false;
}
