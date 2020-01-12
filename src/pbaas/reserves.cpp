/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides reserve currency functions, leveraging the multi-precision boost libraries to calculate reserve currency conversions.
 * 
 */

#include "main.h"
#include "pbaas/pbaas.h"
#include "pbaas/reserves.h"
#include "rpc/server.h"
#include "key_io.h"

CReserveOutput::CReserveOutput(const UniValue &obj) : flags(0)
{
    flags = uni_get_int(find_value(obj, "isvalid")) ? flags | VALID : flags & ~VALID;
    nValue = AmountFromValue(find_value(obj, "value"));
}

UniValue CReserveOutput::ToUniValue() const
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", (bool)(flags & VALID)));
    ret.push_back(Pair("value", ValueFromAmount(nValue)));
    return ret;
}

UniValue CReserveTransfer::ToUniValue() const
{
    UniValue ret(((CReserveOutput *)this)->ToUniValue());
    ret.push_back(Pair("convert", (bool)(flags & CONVERT)));
    ret.push_back(Pair("preconvert", (bool)(flags & PRECONVERT)));
    ret.push_back(Pair("feeoutput", (bool)(flags & FEE_OUTPUT)));
    ret.push_back(Pair("sendback", (bool)(flags & SEND_BACK)));
    return ret;
}

CAmount CReserveTransfer::CalculateFee(uint32_t flags, CAmount transferTotal, const CPBaaSChainDefinition &chainDef)
{
    // determine fee for this send
    if (flags & FEE_OUTPUT)
    {
        return 0;
    }
    CAmount transferFee = CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;

    // take conversion fees for preconvert
    if (flags & CReserveTransfer::PRECONVERT)
    {
        transferFee += CReserveTransactionDescriptor::CalculateConversionFee(transferTotal - transferFee);
    }
    return transferFee;
}

CAmount CReserveTransfer::CalculateTransferFee(uint32_t flags, CAmount transferTotal, const CPBaaSChainDefinition &chainDef)
{
    // determine fee for this send
    if (flags & FEE_OUTPUT)
    {
        return 0;
    }
    return CReserveTransfer::DEFAULT_PER_STEP_FEE << 1;
}

UniValue CReserveExchange::ToUniValue() const
{
    UniValue ret(((CReserveOutput *)this)->ToUniValue());
    ret.push_back(Pair("toreserve", (bool)(flags & TO_RESERVE)));
    ret.push_back(Pair("tonative", !((bool)(flags & TO_RESERVE))));
    ret.push_back(Pair("limitorder", (bool)(flags & LIMIT)));
    if (flags & LIMIT)
    {
        ret.push_back(Pair("limitprice", ValueFromAmount(nLimit)));
    }
    ret.push_back(Pair("fillorkill", (bool)(flags & FILL_OR_KILL)));
    if (flags & FILL_OR_KILL)
    {
        ret.push_back(Pair("validbeforeblock", (int32_t)nValidBefore));
    }
    ret.push_back(Pair("sendoutput", (bool)(flags & SEND_OUTPUT)));
    return ret;
}

CReserveExchange::CReserveExchange(const CTransaction &tx, bool validate)
{
    bool orderFound = false;
    for (auto out : tx.vout)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(out.scriptPubKey, p))
        {
            if (p.evalCode == EVAL_RESERVE_EXCHANGE)
            {
                if (orderFound)
                {
                    flags &= !VALID;        // invalidate
                }
                else
                {
                    FromVector(p.vData[0], *this);
                    orderFound = true;
                }
            }
        }
    }

    if (validate)
    {
        
    }
}

UniValue CCrossChainExport::ToUniValue() const
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chainid", chainID.GetHex()));
    obj.push_back(Pair("numinputs", numInputs));
    obj.push_back(Pair("totalamount", ValueFromAmount(totalAmount)));
    obj.push_back(Pair("totalfees", ValueFromAmount(totalFees)));
    return obj;
}

UniValue CCrossChainImport::ToUniValue() const
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chainid", chainID.GetHex()));
    obj.push_back(Pair("valuein", ValueFromAmount(nValue)));
    return obj;
}

CCrossChainImport::CCrossChainImport(const CTransaction &tx)
{
    for (auto out : tx.vout)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(out.scriptPubKey, p))
        {
            // always take the first for now
            if (p.evalCode == EVAL_CROSSCHAIN_IMPORT && p.vData.size())
            {
                FromVector(p.vData[0], *this);
            }
        }
    }
}

CCurrencyState::CCurrencyState(const UniValue &obj)
{
    flags = uni_get_int(find_value(obj, "flags"));
    int32_t initialRatio = AmountFromValue(find_value(obj, "initialratio"));
    if (initialRatio > CReserveExchange::SATOSHIDEN)
    {
        initialRatio = CReserveExchange::SATOSHIDEN;
    }
    else if (initialRatio < MIN_RESERVE_RATIO)
    {
        initialRatio = MIN_RESERVE_RATIO;
    }
    InitialRatio = initialRatio;

    InitialSupply = AmountFromValue(find_value(obj, "initialsupply"));
    Emitted = AmountFromValue(find_value(obj, "emitted"));
    Supply = AmountFromValue(find_value(obj, "supply"));
    Reserve = AmountFromValue(find_value(obj, "reserve"));
}

UniValue CCurrencyState::ToUniValue() const
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("flags", (int32_t)flags));
    ret.push_back(Pair("initialsupply", ValueFromAmount(InitialSupply)));
    ret.push_back(Pair("emitted", ValueFromAmount(Emitted)));
    ret.push_back(Pair("supply", ValueFromAmount(Supply)));

    if (IsReserve())
    {
        ret.push_back(Pair("initialratio", ValueFromAmount(InitialRatio)));
        ret.push_back(Pair("reserve", ValueFromAmount(Reserve)));
        ret.push_back(Pair("priceinreserve", ValueFromAmount(PriceInReserve())));
    }
    return ret;
}

CCoinbaseCurrencyState::CCoinbaseCurrencyState(const CTransaction &tx, int *pOutIdx)
{
    int localIdx;
    int &i = pOutIdx ? *pOutIdx : localIdx;
    for (i = 0; i < tx.vout.size(); i++)
    {
        COptCCParams p;
        if (IsPayToCryptoCondition(tx.vout[i].scriptPubKey, p))
        {
            if (p.evalCode == EVAL_CURRENCYSTATE && p.vData.size())
            {
                FromVector(p.vData[0], *this);
                break;
            }
        }
    }
}

CCoinbaseCurrencyState::CCoinbaseCurrencyState(const UniValue &obj) : CCurrencyState(obj)
{
    ReserveIn = AmountFromValue(find_value(obj, "reservein"));
    NativeIn = AmountFromValue(find_value(obj, "nativein"));
    ReserveOut = CReserveOutput(find_value(obj, "reserveout"));
    ConversionPrice = AmountFromValue(find_value(obj, "lastconversionprice"));
    Fees = AmountFromValue(find_value(obj, "fees"));
    ConversionFees = AmountFromValue(find_value(obj, "conversionfees"));
}

UniValue CCoinbaseCurrencyState::ToUniValue() const
{
    UniValue ret(UniValue::VOBJ);
    ret = ((CCurrencyState *)this)->ToUniValue();
    ret.push_back(Pair("reservein", ValueFromAmount(ReserveIn)));
    ret.push_back(Pair("nativein", ValueFromAmount(NativeIn)));
    ret.push_back(Pair("reserveout", ReserveOut.ToUniValue()));
    ret.push_back(Pair("lastconversionprice", ValueFromAmount(ConversionPrice)));
    ret.push_back(Pair("fees", ValueFromAmount(Fees)));
    ret.push_back(Pair("conversionfees", ValueFromAmount(ConversionFees)));
    return ret;
}

// This can handle multiple aggregated, bidirectional conversions in one block of transactions. To determine the conversion price, it 
// takes both input amounts of the reserve and the fractional currency to merge the conversion into one calculation
// with the same price for all transactions in the block. It returns the newly calculated conversion price of the fractional 
// reserve in the reserve currency.
CAmount CCurrencyState::ConvertAmounts(CAmount inputReserve, CAmount inputFractional, CCurrencyState &newState) const
{
    newState = *this;
    CAmount conversionPrice = PriceInReserve();

    int64_t totalFractionalOut = 0;     // how much fractional goes to buyers
    int64_t totalReserveOut = 0;        // how much reserve goes to sellers

    // if both conversions are zero, nothing to do but return current price
    if ((!inputReserve && !inputFractional) || !(flags & ISRESERVE))
    {
        return conversionPrice;
    }

    // first, cancel out all parity at the current price, leaving one or the other amount to be converted, but not both
    // that results in consistently minimum slippage in either direction
    CAmount reserveOffset = NativeToReserve(inputFractional, conversionPrice);
    CAmount convertFractional = 0, convertReserve = 0;
    if (inputReserve >= reserveOffset)
    {
        convertReserve = inputReserve - reserveOffset;
        totalFractionalOut += inputFractional;
    }
    else
    {
        CAmount fractionalOffset = ReserveToNative(inputReserve, conversionPrice);
        convertFractional = inputFractional - fractionalOffset;
        if (convertFractional < 0)
        {
            convertFractional = 0;
        }
        totalReserveOut += inputReserve;
    }

    if (!convertReserve && !convertFractional)
    {
        return conversionPrice;
    }

    cpp_dec_float_50 reservein(std::to_string(convertReserve));
    cpp_dec_float_50 fractionalin(std::to_string(convertFractional));
    cpp_dec_float_50 supply(std::to_string((Supply)));
    cpp_dec_float_50 reserve(std::to_string(Reserve));
    cpp_dec_float_50 ratio = GetReserveRatio();
    cpp_dec_float_50 one("1");
    cpp_dec_float_50 dec_satoshi(std::to_string(CReserveExchange::SATOSHIDEN));

    // first check if anything to buy
    if (convertReserve)
    {
        cpp_dec_float_50 supplyout;
        supplyout = (supply * (pow((reservein / reserve) + one, ratio) - one));

        int64_t supplyOut;
        if (!to_int64(supplyout, supplyOut))
        {
            assert(false);
        }
        totalFractionalOut += supplyOut;

        cpp_dec_float_50 dec_price = cpp_dec_float_50(std::to_string(inputReserve)) / cpp_dec_float_50(std::to_string(totalFractionalOut));
        cpp_dec_float_50 reserveFromFractional = cpp_dec_float_50(std::to_string(inputFractional)) * dec_price;
        dec_price = dec_price * dec_satoshi;

        if (!to_int64(reserveFromFractional, totalReserveOut))
        {
            assert(false);
        }

        if (!to_int64(dec_price, conversionPrice))
        {
            assert(false);
        }
    }
    else
    { 
        cpp_dec_float_50 reserveout;
        int64_t reserveOut;
        reserveout = reserve * (one - pow(one - (fractionalin / supply), (one / ratio)));
        if (!to_int64(reserveout, reserveOut))
        {
            assert(false);
        }

        totalReserveOut += reserveOut;

        cpp_dec_float_50 dec_price = cpp_dec_float_50(std::to_string(totalReserveOut)) / cpp_dec_float_50(std::to_string(inputFractional));
        cpp_dec_float_50 fractionalFromReserve = cpp_dec_float_50(std::to_string(inputReserve)) / dec_price;
        dec_price = dec_price * dec_satoshi;

        if (!to_int64(fractionalFromReserve, totalFractionalOut))
        {
            assert(false);
        }

        if (!to_int64(dec_price, conversionPrice))
        {
            assert(false);
        }
    }

    newState.Supply += totalFractionalOut - inputFractional;
    newState.Reserve += inputReserve - totalReserveOut;

    return conversionPrice;
}

// This can handle multiple aggregated, bidirectional conversions in one block of transactions. To determine the conversion price, it 
// takes both input amounts of the reserve and the fractional currency to merge the conversion into one calculation
// with the same price for all transactions in the block. It returns the newly calculated conversion price of the fractional 
// reserve in the reserve currency.
std::vector<CAmount> CCurrencyState::ConvertAmounts(const std::vector<CAmount> &inputReserves, CAmount inputFractional, CCurrencyState &newState) const
{
    newState = *this;
    CAmount conversionPrice = PriceInReserve();

    CAmount inputReserve = inputReserves.size() ? inputReserves[0] : 0;

    int64_t totalFractionalOut = 0;     // how much fractional goes to buyers
    int64_t totalReserveOut = 0;        // how much reserve goes to sellers

    // if both conversions are zero, nothing to do but return current price
    if ((!inputReserve && !inputFractional) || !(flags & ISRESERVE))
    {
        return std::vector<CAmount>({conversionPrice});
    }

    // first, cancel out all parity at the current price, leaving one or the other amount to be converted, but not both
    // that results in consistently minimum slippage in either direction
    CAmount reserveOffset = NativeToReserve(inputFractional, conversionPrice);
    CAmount convertFractional = 0, convertReserve = 0;
    if (inputReserve >= reserveOffset)
    {
        convertReserve = inputReserve - reserveOffset;
        totalFractionalOut += inputFractional;
    }
    else
    {
        CAmount fractionalOffset = ReserveToNative(inputReserve, conversionPrice);
        convertFractional = inputFractional - fractionalOffset;
        if (convertFractional < 0)
        {
            convertFractional = 0;
        }
        totalReserveOut += inputReserve;
    }

    if (!convertReserve && !convertFractional)
    {
        return std::vector<CAmount>({conversionPrice});
    }

    cpp_dec_float_50 reservein(std::to_string(convertReserve));
    cpp_dec_float_50 fractionalin(std::to_string(convertFractional));
    cpp_dec_float_50 supply(std::to_string((Supply)));
    cpp_dec_float_50 reserve(std::to_string(Reserve));
    cpp_dec_float_50 ratio = GetReserveRatio();
    cpp_dec_float_50 one("1");
    cpp_dec_float_50 dec_satoshi(std::to_string(CReserveExchange::SATOSHIDEN));

    // first check if anything to buy
    if (convertReserve)
    {
        cpp_dec_float_50 supplyout;
        supplyout = (supply * (pow((reservein / reserve) + one, ratio) - one));

        int64_t supplyOut;
        if (!to_int64(supplyout, supplyOut))
        {
            assert(false);
        }
        totalFractionalOut += supplyOut;

        cpp_dec_float_50 dec_price = cpp_dec_float_50(std::to_string(inputReserve)) / cpp_dec_float_50(std::to_string(totalFractionalOut));
        cpp_dec_float_50 reserveFromFractional = cpp_dec_float_50(std::to_string(inputFractional)) * dec_price;
        dec_price = dec_price * dec_satoshi;

        if (!to_int64(reserveFromFractional, totalReserveOut))
        {
            assert(false);
        }

        if (!to_int64(dec_price, conversionPrice))
        {
            assert(false);
        }
    }
    else
    { 
        cpp_dec_float_50 reserveout;
        int64_t reserveOut;
        reserveout = reserve * (one - pow(one - (fractionalin / supply), (one / ratio)));
        if (!to_int64(reserveout, reserveOut))
        {
            assert(false);
        }

        totalReserveOut += reserveOut;

        cpp_dec_float_50 dec_price = cpp_dec_float_50(std::to_string(totalReserveOut)) / cpp_dec_float_50(std::to_string(inputFractional));
        cpp_dec_float_50 fractionalFromReserve = cpp_dec_float_50(std::to_string(inputReserve)) / dec_price;
        dec_price = dec_price * dec_satoshi;

        if (!to_int64(fractionalFromReserve, totalFractionalOut))
        {
            assert(false);
        }

        if (!to_int64(dec_price, conversionPrice))
        {
            assert(false);
        }
    }

    newState.Supply += totalFractionalOut - inputFractional;
    newState.Reserve += inputReserve - totalReserveOut;

    return std::vector<CAmount>({conversionPrice});
}

void CReserveTransactionDescriptor::AddReserveExchange(const CReserveExchange &rex, int32_t outputIndex, int32_t nHeight)
{
    CAmount fee = 0;

    bool wasMarket = IsMarket();

    flags |= IS_RESERVE + IS_RESERVEEXCHANGE;

    if (IsLimit() || rex.flags & CReserveExchange::LIMIT)
    {
        if (wasMarket || (vRex.size() && (vRex.back().second.flags != rex.flags || (vRex.back().second.nValidBefore != rex.nValidBefore))))
        {
            flags |= IS_REJECT;
            return;
        }

        flags |= IS_LIMIT;

        if (rex.nValidBefore > nHeight)
        {
            flags |= IS_FILLORKILL;
            // fill or kill fee must be pre-subtracted, but is added back on success, making conversion the only
            // requred fee on success
            fee = CalculateConversionFee(rex.nValue + rex.FILL_OR_KILL_FEE);
            if (rex.flags & CReserveExchange::TO_RESERVE)
            {
                numSells += 1;
                nativeConversionFees += fee;
                nativeOutConverted += (rex.nValue + rex.FILL_OR_KILL_FEE) - fee;    // only explicitly converted
                nativeOut += rex.nValue + rex.FILL_OR_KILL_FEE;             // output is always less fees
            }
            else
            {
                numBuys += 1;
                reserveConversionFees += fee;
                reserveOutConverted += rex.nValue + rex.FILL_OR_KILL_FEE - fee;
                reserveOut += rex.nValue + rex.FILL_OR_KILL_FEE;
            }
        }
        else
        {
            flags &= !IS_RESERVEEXCHANGE;                       // no longer a reserve exchange transaction, falls back to normal reserve tx
            flags |= IS_FILLORKILLFAIL;

            if (rex.flags & CReserveExchange::TO_RESERVE)
            {
                numSells += 1;
                nativeOut += rex.nValue;
            }
            else
            {
                numBuys += 1;
                reserveOut += rex.nValue;
            }
        }
    }
    else
    {
        fee = CalculateConversionFee(rex.nValue);
        if (rex.flags & CReserveExchange::TO_RESERVE)
        {
            numSells += 1;
            nativeConversionFees += fee;
            nativeOutConverted += rex.nValue - fee;     // fee may not be converted from native, so is not included in converted out
            nativeOut += rex.nValue;                    // conversion fee is considered part of the total native out, since it will be attributed to conversion tx
        }
        else
        {
            numBuys += 1;
            if (!(flags & IS_IMPORT))
            {
                reserveConversionFees += fee;
                reserveOutConverted += rex.nValue - fee;
                reserveOut += rex.nValue;
            }
        }
    }                        
    vRex.push_back(std::make_pair(outputIndex, rex));
}

CAmount CReserveTransactionDescriptor::AllFeesAsNative(const CCurrencyState &currencyState) const
{
    return NativeFees() + currencyState.ReserveToNative(ReserveFees());
}

CAmount CReserveTransactionDescriptor::AllFeesAsNative(const CCurrencyState &currencyState, CAmount exchangeRate) const
{
    return NativeFees() + currencyState.ReserveToNative(ReserveFees(), exchangeRate);
}

CAmount CReserveTransactionDescriptor::AllFeesAsReserve(const CCurrencyState &currencyState) const
{
    return ReserveFees() + currencyState.NativeToReserve(NativeFees());
}

CAmount CReserveTransactionDescriptor::AllFeesAsReserve(const CCurrencyState &currencyState, CAmount exchangeRate) const
{
    return ReserveFees() + currencyState.NativeToReserve(NativeFees(), exchangeRate);
}

/*
 * Checks all structural aspects of the reserve part of a transaction that may have reserve inputs and/or outputs
 */
CReserveTransactionDescriptor::CReserveTransactionDescriptor(const CTransaction &tx, const CCoinsViewCache &view, int32_t nHeight) :
        flags(0),
        ptx(NULL),
        numBuys(0),
        numSells(0),
        numTransfers(0),
        reserveIn(0),
        reserveOutConverted(0),
        reserveOut(0),
        nativeIn(0),
        nativeOutConverted(0),
        nativeOut(0),
        nativeConversionFees(0),
        reserveConversionFees(0)
{
    // market conversions can have any number of both buy and sell conversion outputs, this is used to make efficient, aggregated
    // reserve transfer operations with conversion

    // limit conversion outputs may have multiple outputs with different input amounts and destinations, 
    // but they must not be mixed in a transaction with any dissimilar set of conditions on the output, 
    // including mixing with market orders, parity of buy or sell, limit value and validbefore values, 
    // or the transaction is considered invalid

    // no inputs are valid at height 0
    if (!nHeight)
    {
        flags |= IS_REJECT;
        return;
    }

    // reserve exchange transactions cannot run until identity activates
    if (!chainActive.LastTip() ||
        CConstVerusSolutionVector::activationHeight.ActiveVersion(nHeight) < CConstVerusSolutionVector::activationHeight.ACTIVATE_IDENTITY)
    {
        return;
    }

    bool isVerusActive = IsVerusActive();

    CCrossChainImport cci;
    CCrossChainExport ccx;

    CNameReservation nameReservation;
    CIdentity identity;

    flags |= IS_VALID;

    for (int i = 0; i < tx.vout.size(); i++)
    {
        COptCCParams p;

        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition(p) && p.IsValid())
        {
            switch (p.evalCode)
            {
                case EVAL_IDENTITY_RESERVATION:
                {
                    // one name reservation per transaction
                    if (p.version < p.VERSION_V3 || !p.vData.size() || nameReservation.IsValid() || !(nameReservation = CNameReservation(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    if (identity.IsValid())
                    {
                        if (identity.name == nameReservation.name)
                        {
                            flags |= IS_IDENTITY_DEFINITION + IS_HIGH_FEE;
                        }
                        else
                        {
                            flags &= ~IS_VALID;
                            flags |= IS_REJECT;
                            return;
                        }
                    }
                }
                break;

                case EVAL_IDENTITY_PRIMARY:
                {
                    // one identity per transaction
                    if (p.version < p.VERSION_V3 || !p.vData.size() || identity.IsValid() || !(identity = CIdentity(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    flags |= IS_IDENTITY;
                    if (nameReservation.IsValid())
                    {
                        if (identity.name == nameReservation.name)
                        {
                            flags |= IS_IDENTITY_DEFINITION + IS_HIGH_FEE;
                        }
                        else
                        {
                            flags &= ~IS_VALID;
                            flags |= IS_REJECT;
                            return;
                        }
                    }
                }
                break;

                case EVAL_RESERVE_OUTPUT:
                {
                    CReserveOutput ro;
                    if (!p.vData.size() || !(ro = CReserveOutput(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    AddReserveOutput(ro);
                }
                break;

                case EVAL_RESERVE_TRANSFER:
                {
                    CReserveTransfer rt;
                    if (!p.vData.size() || !(rt = CReserveTransfer(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    // on a PBaaS reserve chain, a reserve transfer is always denominated in reserve, as exchange must happen before it is
                    // created. explicit fees in transfer object are for export and import, not initial mining
                    AddReserveTransfer(rt);
                }
                break;

                case EVAL_RESERVE_EXCHANGE:
                {
                    CReserveExchange rex;
                    if (isVerusActive || !p.vData.size() || !(rex = CReserveExchange(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }

                    // if we send the output to the reserve chain, it must be a TO_RESERVE transaction
                    if ((rex.flags & rex.SEND_OUTPUT) && !(rex.flags & rex.TO_RESERVE))
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    AddReserveExchange(rex, i, nHeight);

                    if (IsReject())
                    {
                        flags &= ~IS_VALID;
                        return;
                    }
                }
                break;

                case EVAL_CROSSCHAIN_IMPORT:
                {
                    // if this is an import, add the amount imported to the reserve input and the amount of reserve output as
                    // the amount available to take from this transaction in reserve as an import fee
                    if (flags & IS_IMPORT || !p.vData.size() || !(cci = CCrossChainImport(p.vData[0])).IsValid())
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }

                    flags |= IS_IMPORT;

                    std::vector<CBaseChainObject *> chainObjs;
                    // either a fully valid import with an export or the first import either in block 1 or the chain definition
                    // on the Verus chain
                    if ((nHeight == 1 && tx.IsCoinBase() && !IsVerusActive()) ||
                        (CPBaaSChainDefinition(tx).IsValid() && IsVerusActive()) ||
                        (tx.vout.back().scriptPubKey.IsOpReturn() &&
                        (chainObjs = RetrieveOpRetArray(tx.vout.back().scriptPubKey)).size() >= 2 &&
                        chainObjs[0]->objectType == CHAINOBJ_TRANSACTION &&
                        chainObjs[1]->objectType == CHAINOBJ_CROSSCHAINPROOF))
                    {
                        if (chainObjs.size())
                        {
                            CTransaction &exportTx = ((CChainObject<CTransaction> *)chainObjs[0])->object;
                            std::vector<CBaseChainObject *> exportTransfers;
                            if ((ccx = CCrossChainExport(exportTx)).IsValid() && 
                                exportTx.vout.back().scriptPubKey.IsOpReturn() &&
                                (exportTransfers = RetrieveOpRetArray(exportTx.vout.back().scriptPubKey)).size() &&
                                exportTransfers[0]->objectType == CHAINOBJ_RESERVETRANSFER)
                            {
                                // an import transaction is built from the export list
                                // we can rebuild it and confirm that it matches exactly
                                // we can also subtract all pre-converted reserve from input
                                // and verify that the output matches the proper conversion

                                // get the chain definition of the chain we are importing from
                                std::vector<CTxOut> checkOutputs;

                                if (!AddReserveTransferImportOutputs(ConnectedChains.ThisChain(), exportTransfers, checkOutputs))
                                {
                                    flags |= IS_REJECT;
                                }
                                // TODO:PBAAS - hardening - validate all the outputs we got back as the same as what is in the transaction
                            }
                            else
                            {
                                flags |= IS_REJECT;
                            }
                        }
                    }
                    else
                    {
                        flags |= IS_REJECT;
                    }
                    DeleteOpRetObjects(chainObjs);
                    if (IsReject())
                    {
                        flags &= ~IS_VALID;
                        return;
                    }
                }
                break;

                // this check will need to be made complete by preventing mixing both here and where the others
                // are seen
                case EVAL_CROSSCHAIN_EXPORT:
                {
                    // cross chain export is incompatible with reserve exchange outputs
                    if (IsReserveExchange() ||
                        (!(nHeight == 1 && tx.IsCoinBase() && !isVerusActive) &&
                        !(CPBaaSChainDefinition(tx).IsValid() && isVerusActive) &&
                        (flags & IS_IMPORT)))
                    {
                        flags &= ~IS_VALID;
                        flags |= IS_REJECT;
                        return;
                    }
                    flags |= IS_EXPORT;
                }
                break;
            }
        }
    }

    // we have all inputs, outputs, and fees, if check inputs, we can check all for consistency
    // inputs may be in the memory pool or on the blockchain

    int64_t interest;
    nativeOut = tx.GetValueOut();
    nativeIn = view.GetValueIn(nHeight, &interest, tx);

    // if we don't have a reserve transaction, we're done
    // don't try to replace basic transaction validation
    if (IsReserve())
    {
        int64_t interest;
        CAmount nValueIn = 0;

        if (!(flags & IS_IMPORT))
        {
            LOCK2(cs_main, mempool.cs);

            // if it is a conversion to reserve, the amount in is accurate, since it is from the native coin, if converting to
            // the native PBaaS coin, the amount input is a sum of all the reserve token values of all of the inputs
            reserveIn += view.GetReserveValueIn(nHeight, tx);
        }

        // if we have a valid import, override all normal fee calculations
        // in lieu of the import fee calculations
        if (cci.IsValid() && ccx.IsValid())
        {
            if (!(cci.nValue == ccx.totalAmount + ccx.totalFees))
            {
                printf("%s: export value does not match import value\n", __func__);
                flags &= ~IS_VALID;
                flags |= IS_REJECT;
                return;
            }
        }

        // TODO:PBAAS hardening total minimum required fees as we build the descriptor and
        // reject if not correct
        ptx = &tx;
    }
}

bool CReserveTransactionDescriptor::AddReserveTransferImportOutputs(const CPBaaSChainDefinition &chainDef, const std::vector<CBaseChainObject *> &exportObjects, std::vector<CTxOut> &vOutputs)
{
    // we do not change native in or conversion fees, but we define all of these members
    nativeIn = 0;
    nativeOut = 0;
    reserveIn = 0;
    reserveOut = 0;
    numTransfers = 0;
    CAmount transferFees = 0;
    bool isVerusActive = IsVerusActive();

    CCcontract_info CC;
    CCcontract_info *cp;
    CPubKey pk;
    uint160 chainID = chainDef.GetChainID();

    for (int i = 0; i < exportObjects.size(); i++)
    {
        if (exportObjects[i]->objectType == CHAINOBJ_RESERVETRANSFER)
        {
            CReserveTransfer &curTransfer = ((CChainObject<CReserveTransfer> *)exportObjects[i])->object;

            if (curTransfer.IsValid())
            {
                CTxOut newOut;
                numTransfers++;

                if (curTransfer.flags & curTransfer.FEE_OUTPUT)
                {
                    numTransfers--;
                    // fee comes after everything else, so we should have all numbers ready to calculate
                    // check to be sure that the total fee output matches the import fee expected
                    if (i != exportObjects.size() - 1 || curTransfer.nFees || curTransfer.nValue > transferFees)
                    {
                        // invalid
                        printf("%s: Error with fee output transfer %s\n", __func__, curTransfer.ToUniValue().write().c_str());
                        LogPrintf("%s: Error with fee output transfer %s\n", __func__, curTransfer.ToUniValue().write().c_str());
                        return false;
                    }
                }

                CAmount expectedFee = curTransfer.CalculateFee(curTransfer.flags, curTransfer.nValue + curTransfer.nFees, chainDef);
                if (curTransfer.nFees != expectedFee)
                {
                    printf("%s: Incorrect fee sent with export %s\n", __func__, curTransfer.ToUniValue().write().c_str());
                    LogPrintf("%s: Incorrect fee sent with export %s\n", __func__, curTransfer.ToUniValue().write().c_str());
                    return false;
                }

                transferFees += curTransfer.nFees;

                // if this is a fee output, there are no additional fees and value has already been added to reserveIn through other import transactions' fees
                // this value output should, in total, be equal to the export fee
                if (!(curTransfer.flags & (curTransfer.FEE_OUTPUT | curTransfer.PRECONVERT)))
                {
                    reserveIn += curTransfer.nValue + curTransfer.nFees;
                }

                if (curTransfer.flags & curTransfer.PRECONVERT)
                {
                    // output the amount, minus conversion fees, and generate a normal output that spends the net input of the import as native
                    // difference between all potential value out and what was taken unconverted as a fee in our fee output
                    CAmount nativeConverted = CCurrencyState::ReserveToNative(curTransfer.nValue, chainDef.conversion);

                    reserveIn += curTransfer.nFees;
                    nativeIn += nativeConverted;
                    nativeOut += nativeConverted;
                    newOut = CTxOut(nativeConverted, GetScriptForDestination(curTransfer.destination));

                    // if we are preconverting, and this is not a reserve chain, also send the full value of the preconvert in the payment coin back to
                    // the chain payment address on the originating chain
                    if (!chainDef.IsReserve())
                    {
                        // if this is not a reserve chain, we will emit a reserve output to the chain address
                        // which is sent back to the notarizing chain. this "pays" the chain launch recipient on the launch chain
                        // but only after a successful launch and notarization
                        uint160 destChainID = isVerusActive ? ConnectedChains.ThisChain().GetChainID() : ConnectedChains.NotaryChain().GetChainID();

                        cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);
                        pk = CPubKey(ParseHex(CC.CChexstr));

                        // transfer is to the chain definition, and if we're making it, that will be on the exporting chain, send it back to ourselves from where it's going
                        std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(chainDef.GetConditionID(EVAL_RESERVE_TRANSFER)), CKeyID(destChainID)});

                        CAmount fees = curTransfer.DEFAULT_PER_STEP_FEE << 1;
                        CReserveTransfer rt = CReserveTransfer(CReserveExchange::VALID, curTransfer.nValue - fees, fees, chainDef.address);

                        // add an extra output
                        vOutputs.push_back(MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, 0, pk, dests, rt));
                    }
                }
                else if (curTransfer.flags & curTransfer.CONVERT)
                {
                    // emit a reserve exchange output
                    cp = CCinit(&CC, EVAL_RESERVE_EXCHANGE);
                    pk = CPubKey(ParseHex(CC.CChexstr));

                    std::vector<CTxDestination> dests = std::vector<CTxDestination>({CTxDestination(CKeyID(curTransfer.destination)), CTxDestination(pk)});

                    CAmount conversionFees = CalculateConversionFee(curTransfer.nValue);
                    reserveConversionFees += conversionFees;
                    CReserveExchange rex = CReserveExchange(CReserveExchange::VALID, curTransfer.nValue);

                    reserveOutConverted += curTransfer.nValue - conversionFees;
                    reserveOut += curTransfer.nValue;
                    newOut = MakeCC1ofAnyVout(EVAL_RESERVE_EXCHANGE, 0, dests, rex, pk);
                }
                else if ((curTransfer.flags & curTransfer.SEND_BACK) && curTransfer.nValue >= (curTransfer.DEFAULT_PER_STEP_FEE << 2))
                {
                    // generate a reserve transfer back to the source chain if we have at least double the fee, otherwise leave it on
                    // this chain to be claimed
                    cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);
                    pk = CPubKey(ParseHex(CC.CChexstr));

                    // transfer is to the chain definition, and if we're making it, that will be on the exporting chain, send it back to ourselves from where it's going
                    std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(chainDef.GetConditionID(EVAL_RESERVE_TRANSFER)), CKeyID(ConnectedChains.ThisChain().GetChainID())});

                    reserveOut += curTransfer.nValue;

                    CAmount fees = curTransfer.DEFAULT_PER_STEP_FEE << 1;
                    CReserveTransfer rt = CReserveTransfer(CReserveExchange::VALID, curTransfer.nValue - fees, fees, curTransfer.destination);

                    if ((isVerusActive && chainDef.name != ConnectedChains.ThisChain().name) || (!isVerusActive && chainDef.name != ConnectedChains.NotaryChain().chainDefinition.name))
                    {
                        newOut = MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, 0, pk, dests, rt);                    
                    }
                    else
                    {
                        newOut = MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, curTransfer.nValue, pk, dests, rt);                    
                    }
                }
                else
                {
                    // see if we are creating a reserve import for a PBaaS reserve chain
                    if ((isVerusActive && chainDef.name != ConnectedChains.ThisChain().name) || (!isVerusActive && chainDef.name != ConnectedChains.NotaryChain().chainDefinition.name))
                    {
                        // generate a reserve output of the amount indicated, less fees
                        // we will send using a reserve output, fee will be paid through coinbase by converting from reserve or not, depending on currency settings
                        cp = CCinit(&CC, EVAL_RESERVE_OUTPUT);

                        std::vector<CTxDestination> dests = std::vector<CTxDestination>({CTxDestination(curTransfer.destination)});
                        CReserveOutput ro = CReserveOutput(CReserveExchange::VALID, curTransfer.nValue);

                        // no alternative signer for this, send invalid pubkey
                        newOut = MakeCC1ofAnyVout(EVAL_RESERVE_OUTPUT, 0, dests, ro);

                        reserveOut += curTransfer.nValue;
                    }
                    else
                    {
                        // we are creating an import for the Verus chain to spend from a PBaaS account of a reserve currency, move the value specified, which will spend from
                        // the RESERVE_DEPOSIT outputs
                        newOut = CTxOut(curTransfer.nValue, GetScriptForDestination(curTransfer.destination));

                        reserveOut += curTransfer.nValue;
                    }
                }
                vOutputs.push_back(newOut);
            }
            else
            {
                printf("%s: Invalid reserve transfer on export\n", __func__);
                LogPrintf("%s: Invalid reserve transfer on export\n", __func__);
                return false;
            }
        }
    }

    // double check that the export fee taken as the fee output matches the export fee that should have been taken
    CCrossChainExport ccx(chainDef.GetChainID(), numTransfers, reserveIn - transferFees, transferFees);
    if (reserveIn - reserveOut > ccx.CalculateImportFee())
    {
        printf("%s: Too much fee taken by export -- taken: %lu, import fee: %lu, export fee: %lu\n", __func__, reserveIn - reserveOut, ccx.CalculateImportFee(), ccx.CalculateExportFee());
        LogPrintf("%s: Too much fee taken by export -- taken: %lu, import fee: %lu, export fee: %lu\n", __func__, reserveIn - reserveOut, ccx.CalculateImportFee(), ccx.CalculateExportFee());
        return false;
    }
    return true;
}

CMutableTransaction &CReserveTransactionDescriptor::AddConversionInOuts(CMutableTransaction &conversionTx, std::vector<CInputDescriptor> &conversionInputs, CAmount exchangeRate, const CCurrencyState *pCurrencyState) const
{
    if (!IsReserveExchange() || IsFillOrKillFail())
    {
        return conversionTx;
    }

    CCurrencyState dummy;
    const CCurrencyState &currencyState = pCurrencyState ? *pCurrencyState : dummy;
    // if no exchange rate is specified, first from the currency if present, then it is unity
    if (!exchangeRate)
    {
        int64_t price;
        if (pCurrencyState && (price = currencyState.PriceInReserve()) != 0)
        {
            exchangeRate = price;
        }
        else
        {
            exchangeRate = CReserveExchange::SATOSHIDEN;
        }
    }

    CAmount feesLeft = nativeConversionFees + currencyState.ReserveToNative(reserveConversionFees, exchangeRate);

    uint256 txHash = ptx->GetHash();

    for (auto &indexRex : vRex)
    {
        COptCCParams p;
        ptx->vout[indexRex.first].scriptPubKey.IsPayToCryptoCondition(p);

        CCcontract_info CC;
        CCcontract_info *cp;

        CAmount fee = CalculateConversionFee(indexRex.second.nValue);
        if (fee > feesLeft)
        {
            fee = feesLeft;
        }
        feesLeft -= fee;

        CAmount amount = indexRex.second.nValue - fee;

        // add input...
        conversionTx.vin.push_back(CTxIn(txHash, indexRex.first, CScript()));

        // ... and input descriptor. we leave the CTxIn empty and use the one in the corresponding input, using the input descriptor for only
        // script and value
        conversionInputs.push_back(CInputDescriptor(ptx->vout[indexRex.first].scriptPubKey, ptx->vout[indexRex.first].nValue, CTxIn()));

        // if we should emit a reserve transfer or normal reserve output
        if (indexRex.second.flags & indexRex.second.SEND_OUTPUT)
        {
            assert(indexRex.second.flags & indexRex.second.TO_RESERVE);
            cp = CCinit(&CC, EVAL_RESERVE_TRANSFER);

            amount = currencyState.NativeToReserve(amount, exchangeRate);

            CPubKey pk = CPubKey(ParseHex(CC.CChexstr));

            // send the entire amount to a reserve transfer output of the specific chain
            std::vector<CTxDestination> dests = std::vector<CTxDestination>({CKeyID(ConnectedChains.ThisChain().GetConditionID(EVAL_RESERVE_TRANSFER)), 
                                                                                CKeyID(ConnectedChains.NotaryChain().GetChainID())});

            // create the transfer output with the converted amount less fees
            CReserveTransfer rt(CReserveTransfer::VALID, amount - CReserveTransfer::DEFAULT_PER_STEP_FEE << 1, CReserveTransfer::DEFAULT_PER_STEP_FEE << 1, GetDestinationID(p.vKeys[0]));

            // cast object to the most derived class to avoid template errors to a least derived class
            conversionTx.vout.push_back(MakeCC1of1Vout(EVAL_RESERVE_TRANSFER, 0, pk, dests, (CReserveTransfer)rt));
        }
        else if (indexRex.second.flags & indexRex.second.TO_RESERVE)
        {
            // convert amount to reserve from native
            amount = currencyState.NativeToReserve(amount, exchangeRate);

            // send the net amount to the indicated destination, which is the first entry in the destinations of the original reserve/exchange by protocol
            std::vector<CTxDestination> dests = std::vector<CTxDestination>({p.vKeys[0]});

            // create the output with the unconverted amount less fees
            CReserveOutput ro(CReserveOutput::VALID, amount);

            conversionTx.vout.push_back(MakeCC1ofAnyVout(EVAL_RESERVE_OUTPUT, 0, dests, ro));
        }
        else
        {
            // convert amount to native from reserve and send as normal output
            amount = currencyState.ReserveToNative(amount, exchangeRate);
            conversionTx.vout.push_back(CTxOut(amount, GetScriptForDestination(p.vKeys[0])));
        }
    }
    return conversionTx;
}

// this should be done no more that once to prepare a currency state to be moved to the next state
// emission occurs for a block before any conversion or exchange and that impact on the currency state is calculated
CCurrencyState &CCurrencyState::UpdateWithEmission(CAmount emitted)
{
    InitialSupply = Supply;
    Emitted = 0;

    // if supply is 0, reserve must be zero, and we cannot function as a reserve currency
    if (Supply <= 0 || Reserve <= 0)
    {
        if (Supply < 0)
        {
            Emitted = Supply = emitted;
        }
        else
        {
            Emitted = emitted;
            Supply += emitted;
        }
        if (Reserve > 0 && InitialRatio == 0)
        {
            InitialRatio = Supply / Reserve;
        }
        return *this;
    }

    if (emitted)
    {
        // to balance rounding with truncation, we statistically add a satoshi to the initial ratio
        arith_uint256 bigInitial(InitialRatio);
        arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
        arith_uint256 bigEmission(emitted);
        arith_uint256 bigSupply(Supply);

        arith_uint256 bigScratch = (bigInitial * bigSupply * bigSatoshi) / (bigSupply + bigEmission);
        arith_uint256 bigRatio = bigScratch / bigSatoshi;
        // cap ratio at 1
        if (bigRatio >= bigSatoshi)
        {
            bigScratch = arith_uint256(CReserveExchange::SATOSHIDEN * CReserveExchange::SATOSHIDEN);
            bigRatio = bigSatoshi;
        }

        int64_t newRatio = bigRatio.GetLow64();
        int64_t remainder = bigScratch.GetLow64() - (newRatio * CReserveExchange::SATOSHIDEN);
        // form of bankers rounding, if odd, round up at half, if even, round down at half
        if (remainder > (CReserveExchange::SATOSHIDEN >> 1) || (remainder == (CReserveExchange::SATOSHIDEN >> 1) && newRatio & 1))
        {
            newRatio += 1;
        }

        // update initial supply to be what we currently have
        Emitted = emitted;
        InitialRatio = newRatio;
        Supply = InitialSupply + emitted;
    }
    return *this; 
}

// From a vector of transaction pointers, match all that are valid orders and can be matched,
// put them into a vector of reserve transaction descriptors, and return a new fractional reserve state,
// if pConversionTx is present, this will add one output to it for each transaction included
// and consider its size wen comparing to maxSerializeSize
CCoinbaseCurrencyState CCoinbaseCurrencyState::MatchOrders(const std::vector<const CTransaction *> &orders, 
                                                            std::vector<CReserveTransactionDescriptor> &reserveFills, 
                                                            std::vector<const CTransaction *> &expiredFillOrKills, 
                                                            std::vector<const CTransaction *> &noFills, 
                                                            std::vector<const CTransaction *> &rejects, 
                                                            CAmount &price, int32_t height, std::vector<CInputDescriptor> &conversionInputs,
                                                            int64_t maxSerializeSize, int64_t *pInOutTotalSerializeSize, CMutableTransaction *pConversionTx,
                                                            bool feesAsReserve) const
{
    // synthetic order book of limitBuys and limitSells sorted by limit, order of preference beyond limit sorting is random
    std::multimap<CAmount, CReserveTransactionDescriptor> limitBuys;
    std::multimap<CAmount, CReserveTransactionDescriptor> limitSells;        // limit orders are prioritized by limit
    std::multimap<CAmount, CReserveTransactionDescriptor> marketOrders;      // prioritized by fee rate
    CAmount totalNativeConversionFees = 0;
    CAmount totalReserveConversionFees = 0;

    int64_t totalSerializedSize = pInOutTotalSerializeSize ? *pInOutTotalSerializeSize + CCurrencyState::CONVERSION_TX_SIZE_MIN : CCurrencyState::CONVERSION_TX_SIZE_MIN;
    int64_t conversionSizeOverhead = 0;

    if (!(flags & ISRESERVE))
    {
        return CCoinbaseCurrencyState();
    }

    uint32_t tipTime = (chainActive.Height() >= height) ? chainActive[height]->nTime : chainActive.LastTip()->nTime;
    CCoinsViewCache view(pcoinsTip);

    // organize all valid reserve exchange transactions into 3 multimaps, limitBuys, limitSells, and market orders
    // orders that should be treated as normal will go into refunds and invalid transactions into rejects
    for (int i = 0; i < orders.size(); i++)
    {
        CReserveTransactionDescriptor txDesc(*orders[i], view, height);

        if (txDesc.IsValid() && txDesc.IsReserveExchange())
        {
            // if this is a market order, put it in, if limit, put it in an order book, sorted by limit
            if (txDesc.IsMarket())
            {
                CAmount fee = txDesc.AllFeesAsNative(*this) + ReserveToNative(txDesc.reserveConversionFees) + txDesc.nativeConversionFees;
                CFeeRate feeRate = CFeeRate(fee, GetSerializeSize(CDataStream(SER_NETWORK, PROTOCOL_VERSION), *txDesc.ptx));
                marketOrders.insert(std::make_pair(feeRate.GetFeePerK(), txDesc));
            }
            else
            {
                assert(txDesc.IsLimit());
                // limit order, so put it in buy or sell
                if (txDesc.numBuys)
                {
                    limitBuys.insert(std::make_pair(txDesc.vRex[0].second.nLimit, txDesc));
                }
                else
                {
                    assert(txDesc.numSells);
                    limitSells.insert(std::make_pair(txDesc.vRex[0].second.nLimit, txDesc));
                }
            }
        }
        else if (txDesc.IsValid())
        {
            expiredFillOrKills.push_back(orders[i]);
        }
        else
        {
            rejects.push_back(orders[i]);
        }
    }

    // now we have all market orders in marketOrders multimap, sorted by feePerK, rejects that are expired or invalid in rejects
    // first, prune as many market orders as possible up to the maximum storage space available for market orders, which we calculate
    // as a functoin of the total space available and precentage of total orders that are market orders
    int64_t numLimitOrders = limitBuys.size() + limitSells.size();
    int64_t numMarketOrders = marketOrders.size();

    // if nothing to do, we are done, don't update anything
    if (!(reserveFills.size() + numLimitOrders + numMarketOrders))
    {
        return *this;
    }

    // 1. start from the current state and calculate what the price would be with market orders that fit, leaving room for limit orders
    // 2. add limit orders, first buys, as many as we can from the highest value and number downward, one at a time, until we run out or
    //    cannot add any more due to not meeting the price. then we do the same for sells if there are any available, and alternate until
    //    we either run out or cannot add from either side. within a specific limit, orders are sorted by largest first, which means
    //    there is no point in retracing if an element fails to be added
    // 3. calculate a final order price.5
    // 4. create and return a new, updated CCoinbaseCurrencyState
    CCoinbaseCurrencyState newState(*(CCurrencyState *)this, 0, 0, CReserveOutput(), PriceInReserve(), 0, 0);

    CAmount exchangeRate;

    if (pConversionTx)
    {
        conversionSizeOverhead = GetSerializeSize(*pConversionTx, SER_NETWORK, PROTOCOL_VERSION);
    }
    int64_t curSpace = maxSerializeSize - (totalSerializedSize + conversionSizeOverhead);

    if (curSpace < 0)
    {
        printf("%s: no space available in block to include reserve orders\n", __func__);
        LogPrintf("%s: no space available in block to include reserve orders\n", __func__);
        return *this;
    }

    // the ones that are passed may be any valid reserve related transaction
    // we need to first process those with the assumption that they are included
    for (auto it = reserveFills.begin(); it != reserveFills.end(); )
    {
        auto &txDesc = *it;

        // add up the starting point for conversions
        if (txDesc.IsReserveExchange())
        {
            CMutableTransaction mtx;
            if (pConversionTx)
            {
                mtx = *pConversionTx;
                txDesc.AddConversionInOuts(mtx, conversionInputs);
                conversionSizeOverhead = GetSerializeSize(mtx, SER_NETWORK, PROTOCOL_VERSION);
            }
            int64_t tmpSpace = maxSerializeSize - (totalSerializedSize + conversionSizeOverhead);

            if (tmpSpace < 0)
            {
                // can't fit, so it's a noFill
                noFills.push_back(txDesc.ptx);
                it = reserveFills.erase(it);
            }
            else
            {
                curSpace = tmpSpace;

                totalNativeConversionFees += txDesc.nativeConversionFees;
                totalReserveConversionFees += txDesc.reserveConversionFees;

                if (feesAsReserve)
                {
                    newState.ReserveIn += txDesc.reserveOutConverted;
                    newState.NativeIn += txDesc.nativeOutConverted + txDesc.NativeFees() + txDesc.nativeConversionFees;
                }
                else
                {
                    newState.ReserveIn += txDesc.reserveOutConverted + txDesc.ReserveFees() + txDesc.reserveConversionFees;
                    newState.NativeIn += txDesc.nativeOutConverted;
                }

                if (pConversionTx)
                {
                    *pConversionTx = mtx;
                }
                it++;
            }
        }
        else
        {
            if (feesAsReserve)
            {
                newState.NativeIn += txDesc.NativeFees();
            }
            else
            {
                newState.ReserveIn += txDesc.ReserveFees();
            }
            it++;
        }
    }

    int64_t marketOrdersSizeLimit = curSpace;
    int64_t limitOrdersSizeLimit = curSpace;
    if (numLimitOrders + numMarketOrders)
    {
        marketOrdersSizeLimit = ((arith_uint256(numMarketOrders) * arith_uint256(curSpace)) / arith_uint256(numLimitOrders + numMarketOrders)).GetLow64();
        limitOrdersSizeLimit = curSpace - marketOrdersSizeLimit;
    }
    
    if (limitOrdersSizeLimit < 1024 && maxSerializeSize > 2048)
    {
        marketOrdersSizeLimit = maxSerializeSize - 1024;
        limitOrdersSizeLimit = 1024;
    }

    for (auto marketOrder : marketOrders)
    {
        // add as many as we can fit, if we are able to, consider the output transaction overhead as well
        int64_t thisSerializeSize = GetSerializeSize(*marketOrder.second.ptx, SER_NETWORK, PROTOCOL_VERSION);
        CMutableTransaction mtx;
        if (pConversionTx)
        {
            mtx = *pConversionTx;
            marketOrder.second.AddConversionInOuts(mtx, conversionInputs);
            conversionSizeOverhead = GetSerializeSize(mtx, SER_NETWORK, PROTOCOL_VERSION);
        }
        if ((totalSerializedSize + thisSerializeSize + conversionSizeOverhead) <= marketOrdersSizeLimit)
        {
            totalNativeConversionFees += marketOrder.second.nativeConversionFees;
            totalReserveConversionFees += marketOrder.second.reserveConversionFees;
            if (pConversionTx)
            {
                *pConversionTx = mtx;
            }
            if (feesAsReserve)
            {
                newState.ReserveIn += marketOrder.second.reserveOutConverted;
                newState.NativeIn += marketOrder.second.nativeOutConverted + marketOrder.second.NativeFees() + marketOrder.second.nativeConversionFees;
            }
            else
            {
                newState.ReserveIn += marketOrder.second.reserveOutConverted + marketOrder.second.ReserveFees() + marketOrder.second.reserveConversionFees;
                newState.NativeIn += marketOrder.second.nativeOutConverted;
            }
            reserveFills.push_back(marketOrder.second);
            totalSerializedSize += thisSerializeSize;
        }
        else
        {
            // can't fit, no fill
            noFills.push_back(marketOrder.second.ptx);
        }
    }

    // iteratively add limit orders first buy, then sell, until we no longer have anything to add
    // this must iterate because each time we add a buy, it may put another sell's limit within reach and
    // vice versa
    std::pair<std::multimap<CAmount, CReserveTransactionDescriptor>::iterator, CAmount> buyPartial = make_pair(limitBuys.end(), 0); // valid in loop for partial fill
    std::pair<std::multimap<CAmount, CReserveTransactionDescriptor>::iterator, CAmount> sellPartial = make_pair(limitSells.end(), 0);

    limitOrdersSizeLimit = maxSerializeSize - totalSerializedSize;
    int64_t buyLimitSizeLimit = numLimitOrders ? totalSerializedSize + ((arith_uint256(limitBuys.size()) * arith_uint256(limitOrdersSizeLimit)) / arith_uint256(numLimitOrders)).GetLow64() : limitOrdersSizeLimit;

    CCurrencyState latestState = newState;

    for (bool tryagain = true; tryagain; )
    {
        tryagain = false;

        exchangeRate = ConvertAmounts(newState.ReserveIn, newState.NativeIn, latestState);

        // starting with that exchange rate, add buys one at a time until we run out of buys to add or reach the limit,
        // possibly in a partial fill, then see if we can add any sells. we go back and forth until we have stopped being able to add
        // new orders. it would be possible to recognize a state where we are able to simultaneously fill a buy and a sell that are
        // the the same or very minimally overlapping limits
        
        for (auto limitBuysIt = limitBuys.rbegin(); limitBuysIt != limitBuys.rend() && exchangeRate > limitBuysIt->second.vRex.front().second.nLimit; limitBuysIt = limitBuys.rend())
        {
            // any time there are entries above the lower bound, we only actually look at the end, since we mutate the
            // search space each iteration and remove anything we've already added, making the end always the most in-the-money
            CReserveTransactionDescriptor &currentBuy = limitBuysIt->second;

            // it must first fit, space-wise
            int64_t thisSerializeSize = GetSerializeSize(*(currentBuy.ptx), SER_NETWORK, PROTOCOL_VERSION);

            CMutableTransaction mtx;
            if (pConversionTx)
            {
                mtx = *pConversionTx;
                currentBuy.AddConversionInOuts(mtx, conversionInputs);
                conversionSizeOverhead = GetSerializeSize(mtx, SER_NETWORK, PROTOCOL_VERSION);
            }
            if ((totalSerializedSize + thisSerializeSize + conversionSizeOverhead) <= buyLimitSizeLimit)
            {
                CAmount newReserveIn, newNativeIn;
                if (feesAsReserve)
                {
                    newReserveIn = currentBuy.reserveOutConverted;
                    newNativeIn = currentBuy.nativeOutConverted + currentBuy.NativeFees() + currentBuy.nativeConversionFees;
                }
                else
                {
                    newReserveIn = currentBuy.reserveOutConverted + currentBuy.ReserveFees() + currentBuy.reserveConversionFees;
                    newNativeIn = currentBuy.nativeOutConverted;
                }

                // calculate fresh with all conversions together to see if we still meet the limit
                CAmount newExchange = ConvertAmounts(newState.ReserveIn + newReserveIn, newState.NativeIn + newNativeIn, latestState);

                if (newExchange <= currentBuy.vRex[0].second.nLimit)
                {
                    totalNativeConversionFees += currentBuy.nativeConversionFees;
                    totalReserveConversionFees += currentBuy.reserveConversionFees;

                    // update conversion transaction if we have one
                    if (pConversionTx)
                    {
                        *pConversionTx = mtx;
                    }

                    // add to the current buys, we will never do something to disqualify this, since all orders left are either
                    // the same limit or lower
                    reserveFills.push_back(currentBuy);
                    totalSerializedSize += thisSerializeSize;

                    newState.ReserveIn += newReserveIn;
                    newState.NativeIn += newNativeIn;

                    exchangeRate = newExchange;
                    limitBuys.erase(--limitBuys.end());
                }
                else
                {
                    // TODO:PBAAS support partial fills
                    break;
                }
            }
        }

        // now, iterate from lowest/most qualified sell limit first to highest/least qualified and attempt to put all in
        // if we can only fill an order partially, then do so
        for (auto limitSellsIt = limitSells.begin(); limitSellsIt != limitSells.end() && exchangeRate > limitSellsIt->second.vRex.front().second.nLimit; limitSellsIt = limitSells.begin())
        {
            CReserveTransactionDescriptor &currentSell = limitSellsIt->second;

            int64_t thisSerializeSize = GetSerializeSize(*currentSell.ptx, SER_NETWORK, PROTOCOL_VERSION);

            CMutableTransaction mtx;
            if (pConversionTx)
            {
                mtx = *pConversionTx;
                currentSell.AddConversionInOuts(mtx, conversionInputs);
                conversionSizeOverhead = GetSerializeSize(mtx, SER_NETWORK, PROTOCOL_VERSION);
            }

            if ((totalSerializedSize + thisSerializeSize + conversionSizeOverhead) <= maxSerializeSize)
            {
                CAmount newReserveIn, newNativeIn;
                if (feesAsReserve)
                {
                    newReserveIn = currentSell.reserveOutConverted;
                    newNativeIn = currentSell.nativeOutConverted + currentSell.NativeFees() + currentSell.nativeConversionFees;
                }
                else
                {
                    newReserveIn = currentSell.reserveOutConverted + currentSell.ReserveFees() + currentSell.reserveConversionFees;
                    newNativeIn = currentSell.nativeOutConverted;
                }

                // calculate fresh with all conversions together to see if we still meet the limit
                CAmount newExchange = ConvertAmounts(newState.ReserveIn + newReserveIn, newState.NativeIn + newNativeIn, latestState);
                if (newExchange >= currentSell.vRex.front().second.nLimit)
                {
                    totalNativeConversionFees += currentSell.nativeConversionFees;
                    totalReserveConversionFees += currentSell.reserveConversionFees;

                    // update conversion transaction if we have one
                    if (pConversionTx)
                    {
                        *pConversionTx = mtx;
                    }

                    // add to the current sells, we will never do something to disqualify this, since all orders left are either
                    // the same limit or higher
                    reserveFills.push_back(currentSell);
                    totalSerializedSize += thisSerializeSize;
                    exchangeRate = newExchange;

                    newState.ReserveIn += newReserveIn;
                    newState.NativeIn += newNativeIn;

                    limitSells.erase(limitSellsIt);
                    tryagain = true;
                }
                else
                {
                    // we must be done with buys whether we created a partial or not
                    break;
                }
            }
        }
        buyLimitSizeLimit = maxSerializeSize - totalSerializedSize;
    }

    // we can calculate total fees now, but to avoid a rounding error when converting native to reserve or vice versa on fees, 
    // we must loop through once more to calculate all fees for the currency state
    CAmount totalFees = 0;
    CAmount reserveOutVal = 0;
    for (auto fill : reserveFills)
    {
        reserveOutVal += NativeToReserve(fill.nativeOutConverted, exchangeRate);
        if (feesAsReserve)
        {
            totalFees += fill.AllFeesAsReserve(newState, exchangeRate);
        }
        else
        {
            totalFees += fill.AllFeesAsNative(newState, exchangeRate);
        }    
    }
    CAmount totalConversionFees;
    if (feesAsReserve)
    {
        totalConversionFees = totalReserveConversionFees + NativeToReserve(totalNativeConversionFees, exchangeRate);
        totalFees += totalConversionFees;
        reserveOutVal += totalReserveConversionFees + NativeToReserve(totalNativeConversionFees, exchangeRate);
    }
    else
    {
        totalConversionFees = totalNativeConversionFees + ReserveToNative(totalReserveConversionFees, exchangeRate);
        totalFees += totalConversionFees;
    }

    newState = CCoinbaseCurrencyState(latestState, newState.ReserveIn, newState.NativeIn, CReserveOutput(CReserveOutput::VALID, reserveOutVal), exchangeRate, totalFees, totalConversionFees);

    price = exchangeRate;

    for (auto entry : limitBuys)
    {
        noFills.push_back(entry.second.ptx);
    }

    for (auto entry : limitSells)
    {
        noFills.push_back(entry.second.ptx);
    }

    // if no matches, no state updates
    if (!reserveFills.size())
    {
        return *this;
    }
    else
    {
        if (pInOutTotalSerializeSize)
        {
            *pInOutTotalSerializeSize = totalSerializedSize;
        }
        printf("%s: %s\n", __func__, newState.ToUniValue().write(1, 2).c_str());
        return newState;
    }
}

CAmount CCurrencyState::CalculateConversionFee(CAmount inputAmount, bool convertToNative) const
{
    arith_uint256 bigAmount(inputAmount);
    arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);

    // we need to calculate a fee based either on the amount to convert or the last price
    // times the reserve
    if (convertToNative)
    {
        int64_t price;
        cpp_dec_float_50 priceInReserve = GetPriceInReserve();
        if (!to_int64(priceInReserve, price))
        {
            assert(false);
        }
        bigAmount = price ? (bigAmount * bigSatoshi) / arith_uint256(price) : 0;
    }

    CAmount fee = 0;
    fee = ((bigAmount * arith_uint256(CReserveExchange::SUCCESS_FEE)) / bigSatoshi).GetLow64();
    if (fee < CReserveExchange::MIN_SUCCESS_FEE)
    {
        fee = CReserveExchange::MIN_SUCCESS_FEE;
    }
    return fee;
}

CAmount CReserveTransactionDescriptor::CalculateConversionFee(CAmount inputAmount)
{
    arith_uint256 bigAmount(inputAmount);
    arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);

    CAmount fee = 0;
    fee = ((bigAmount * arith_uint256(CReserveExchange::SUCCESS_FEE)) / bigSatoshi).GetLow64();
    if (fee < CReserveExchange::MIN_SUCCESS_FEE)
    {
        fee = CReserveExchange::MIN_SUCCESS_FEE;
    }
    return fee;
}

// this calculates a fee that will be added to an amount and result in the same percentage as above,
// such that a total of the inputAmount + this returned fee, if passed to CalculateConversionFee, would return
// the same amount
CAmount CReserveTransactionDescriptor::CalculateAdditionalConversionFee(CAmount inputAmount)
{
    arith_uint256 bigAmount(inputAmount);
    arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
    arith_uint256 conversionFee(CReserveExchange::SUCCESS_FEE);

    CAmount newAmount = ((bigAmount * bigSatoshi) / (bigSatoshi - conversionFee)).GetLow64();
    CAmount fee = CalculateConversionFee(newAmount);
    newAmount = inputAmount + fee;
    fee = CalculateConversionFee(newAmount);            // again to account for minimum fee
    fee += inputAmount - (newAmount - fee);             // add any additional difference
    return fee;
}

CAmount CCurrencyState::ReserveToNative(CAmount reserveAmount) const
{
    static arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
    arith_uint256 bigAmount(reserveAmount);

    int64_t price = PriceInReserve();
    bigAmount = price ? (bigAmount * bigSatoshi) / arith_uint256(price) : 0;

    return bigAmount.GetLow64();
}

CAmount CCurrencyState::ReserveToNative(CAmount reserveAmount, CAmount exchangeRate)
{
    static arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
    arith_uint256 bigAmount(reserveAmount);

    bigAmount = exchangeRate ? (bigAmount * bigSatoshi) / arith_uint256(exchangeRate) : 0;
    return bigAmount.GetLow64();
}

CAmount CCurrencyState::ReserveFeeToNative(CAmount inputAmount, CAmount outputAmount) const
{
    return ReserveToNative(inputAmount - outputAmount);
}
