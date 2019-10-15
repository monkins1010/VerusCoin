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

#ifndef INDENTITY_H
#define INDENTITY_H

#include <univalue.h>
#include <sstream>
#include "streams.h"
#include "pubkey.h"
#include "base58.h"
#include "key_io.h"
#include "crosschainrpc.h"
#include "zcash/Address.hpp"
#include "script/script.h"
#include "primitives/transaction.h"
#include "primitives/transaction.h"
#include "cc/CCinclude.h"

// this includes the necessary data for a principal to sign, but does not
// include enough information to derive a persistent identity
class CPrincipal
{
public:
    static const uint8_t VERSION_INVALID = 0;
    static const uint8_t VERSION_CURRENT = 1;
    static const uint8_t VERSION_FIRSTVALID = 1;
    static const uint8_t VERSION_LASTVALID = 1;

    uint32_t nVersion;
    uint32_t flags;

    // signing authority
    std::vector<CTxDestination> primaryAddresses;
    int32_t minSigs;

    CPrincipal() : nVersion(VERSION_INVALID) {}

    CPrincipal(uint32_t Version,
               uint32_t Flags,
               const std::vector<CTxDestination> &primary, 
               int32_t minPrimarySigs) :
               nVersion(Version),
               flags(Flags),
               primaryAddresses(primary),
               minSigs(minPrimarySigs)
        {}

    CPrincipal(const UniValue &uni);
    CPrincipal(std::vector<unsigned char> asVector)
    {
        ::FromVector(asVector, *this);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(flags);
        std::vector<std::vector<unsigned char>> addressVs;
        if (ser_action.ForRead())
        {
            READWRITE(addressVs);

            for (auto vec : addressVs)
            {
                if (vec.size() == 20)
                {
                    primaryAddresses.push_back(CTxDestination(CKeyID(uint160(vec))));
                }
                else if (vec.size() == 33)
                {
                    primaryAddresses.push_back(CTxDestination(CPubKey(vec)));
                }
                else
                {
                    primaryAddresses.push_back(CTxDestination(CNoDestination()));
                }
            }
        }
        else
        {
            for (auto dest : primaryAddresses)
            {
                addressVs.push_back(GetDestinationBytes(dest));
            }

            READWRITE(addressVs);
        }
        READWRITE(minSigs);
    }

    UniValue ToUniValue() const;

    bool IsValid() const
    {
        return (nVersion > VERSION_FIRSTVALID || nVersion < VERSION_LASTVALID);
    }
};

class CIdentity : public CPrincipal
{
public:
    static const uint8_t FLAG_REVOKED = 0x80;

    uint160 parent;

    // real name or pseudonym, must be unique on the blockchain on which it is defined and can be used
    // as a name for blockchains or other purposes on any chain in the Verus ecosystem once exported to
    // the Verus chain. The hash of this name hashed with the parent if present, must equal the principal ID
    //
    // this name is always normalized to lower case. any identity that is registered
    // on a PBaaS chain may be exported to the Verus chain and referenced by appending
    // a dot ".", followed by the chain name on which this identity was registered.
    // that will eventually create a domain name system as follows:
    // root == VRSC (implicit), so a name that is defined on the Verus chain may be referenced as
    //         just its name.
    //
    // name from another chain == name.chainname
    //      the chainID that would derive from such a name will be Hash(ChainID(chainname) + Hash(name));
    // 
    std::string name;

    // real name/nicknames/handles
    std::vector<std::string> names;

    // revocation authority - can only invalidate identity or update revocation
    uint160 revocationAuthority;

    // recovery authority - can change primary addresses in case of primary key loss or compromise
    uint160 recoveryAuthority;

    // z-address for contact and privately made attestations that can be proven to others
    libzcash::SaplingPaymentAddress privateAddress;

    CIdentity() : CPrincipal() {}

    CIdentity(uint32_t Version,
              uint32_t Flags,
              const std::vector<CTxDestination> &primary,
              int32_t minPrimarySigs,
              const uint160 &Parent,
              const std::string &Name,
              const std::vector<std::string> &Aliases,
              const uint160 &Revocation,
              const uint160 &Recovery,
              const libzcash::SaplingPaymentAddress &Inbox) : 
              CPrincipal(Version, Flags, primary, minPrimarySigs),
              parent(Parent),
              name(Name),
              names(Aliases),
              revocationAuthority(Revocation),
              recoveryAuthority(Recovery),
              privateAddress(Inbox)
    {}

    CIdentity(const UniValue &uni);
    CIdentity(const CTransaction &tx);
    CIdentity(const CScript &scriptPubKey);
    CIdentity(const std::vector<unsigned char> &asVector)
    {
        ::FromVector(asVector, *this);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CPrincipal *)this);
        READWRITE(parent);
        READWRITE(name);
        READWRITE(names);
        READWRITE(revocationAuthority);
        READWRITE(recoveryAuthority);
        READWRITE(privateAddress);
    }

    UniValue ToUniValue() const;

    void Revoke()
    {
        flags |= FLAG_REVOKED;
    }

    void Unrevoke()
    {
        flags &= ~FLAG_REVOKED;
    }

    bool IsRevoked() const
    {
        return flags & FLAG_REVOKED;
    }

    bool IsValidUnrevoked() const
    {
        return IsValid() && !IsRevoked();
    }

    uint160 GetNameID() const;
    uint160 GetNameID(const std::string &name) const;

    CIdentity LookupIdentity(const std::string &name);
    static CIdentity LookupIdentity(const uint160 &nameID);

    CIdentity RevocationAuthority() const
    {
        return GetNameID() == revocationAuthority ? *this : LookupIdentity(revocationAuthority);
    }

    CIdentity RecoveryAuthority() const
    {
        return GetNameID() == recoveryAuthority ? *this : LookupIdentity(recoveryAuthority);
    }

    template <typename TOBJ>
    CTxOut TransparentOutput(uint8_t evalcode, CAmount nValue, const TOBJ &obj) const
    {
        CTxOut ret;

        if (IsValid())
        {
            // this will be output to a hash of the name and serve as its index as well
            CCcontract_info CC;
            CCcontract_info *cp;
            CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

            cp = CCinit(&CC, evalcode);
            pk = CPubKey(ParseHex(CC.CChexstr));

            ret = MakeCCMofNVout(evalcode, nValue, primaryAddresses, CKeyID(CCrossChainRPCData::GetConditionID(GetNameID(), EVAL_IDENTITY_PRIMARY)), minSigs, obj);
        }
        return ret;
    }

    // creates an output script to control updates to this identity
    CScript IdentityUpdateOutputScript() const;
};

#endif // INDENTITY_H
