/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides reserve currency functions, leveraging the multi-precision boost libraries to calculate reserve currency conversions
 * in a predictable manner that can achieve consensus.
 * 
 */

#ifndef PBAAS_RESERVES_H
#define PBAAS_RESERVES_H

#include <sstream>
#include "pbaas/crosschainrpc.h"
#include <boost/multiprecision/cpp_dec_float.hpp>
#include "librustzcash.h"
#include "pubkey.h"

using boost::multiprecision::cpp_dec_float_50;
class CCoinsViewCache;
class CInputDescriptor;
class CBaseChainObject;

// reserve output is a special kind of token output that does not carry it's identifier, as it
// is always assumed to be the reserve currency of the current chain.
class CReserveOutput
{
public:
    static const uint32_t CURRENCY_MASK = 0xffff;
    static const uint32_t VALID = 0x10000;
    static const uint32_t NATIVE = 0x8000;

    uint32_t flags;                 // information about this currency
    CAmount nValue;                 // amount of input reserve coins this UTXO represents before any conversion

    CReserveOutput(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveOutput(const UniValue &obj);

    CReserveOutput() : flags(0), nValue(0) { }

    CReserveOutput(uint32_t Flags, CAmount value) : flags(Flags), nValue(value) { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(flags);
        READWRITE(VARINT(nValue));
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    UniValue ToUniValue() const;

    bool IsNative() const
    {
        return (flags & CURRENCY_MASK) == NATIVE;
    }

    int CurrencyIndex() const
    {
        return (flags & CURRENCY_MASK);
    }

    bool IsValid() const
    {
        // we don't support op returns, value must be in native or reserve
        return (flags & VALID) && ((nValue != 0) || IsNative());
    }
};

class CReserveTransfer : public CReserveOutput
{
public:
    static const uint32_t CONVERT = 0x80000;
    static const uint32_t PRECONVERT = 0x100000;
    static const uint32_t FEE_OUTPUT = 0x200000;        // one per import, amount must match total percentage of fees for exporter, no pre-convert allowed
    static const uint32_t SEND_BACK = 0x400000;         // fee is sent back immediately to destination on exporting chain

    static const CAmount DEFAULT_PER_STEP_FEE = 10000;  // default fee for each step of each transfer (initial mining, transfer, mining on new chain)

    CAmount nFees;                                      // cross-chain network fees only, separated out to enable market conversions, conversion fees are additional
    CKeyID destination;                                 // transparent address to send funds to on the target chain

    CReserveTransfer(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveTransfer() : CReserveOutput() { }

    CReserveTransfer(uint32_t Flags, CAmount value, CAmount fees, CKeyID dest) : CReserveOutput(Flags, value), nFees(fees), destination(dest) { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CReserveOutput *)this);
        READWRITE(VARINT(nFees));
        READWRITE(destination);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    UniValue ToUniValue() const;

    static CAmount CalculateFee(uint32_t flags, CAmount transferTotal, const CPBaaSChainDefinition &chainDef);
    static CAmount CalculateTransferFee(uint32_t flags, CAmount transferTotal, const CPBaaSChainDefinition &chainDef);

    bool IsValid() const
    {
        return CReserveOutput::IsValid() && (nFees > 0 || flags & FEE_OUTPUT) && !destination.IsNull();
    }
};

// convert from $VRSC to fractional reserve coin or vice versa. coinID determines which
// in either direction, this is burned in the block. if burned, the block must contain less than a
// maximum reasonable number of exchange outputs, which must be sorted, hashed, and used to validate
// the outputs that must match exactly on any transaction spending the output. since fees are not
// included in outputs, it is assumed that a miner will spend the output in the same block to recover
// exchange fees
class CReserveExchange : public CReserveOutput
{
public:
    // flags
    static const int32_t TO_RESERVE = 0x80000;      // from fractional currency to reserve, default is reserve to fractional
    static const int32_t LIMIT = 0x100000;          // observe the limit when converting
    static const int32_t FILL_OR_KILL = 0x200000;   // if not filled before nValidBefore but before expiry, no execution, mined with fee, output pass through
    static const int32_t ALL_OR_NONE = 0x400000;    // will not execute partial order
    static const int32_t SEND_OUTPUT = 0x800000;    // send the output of this exchange to the target chain, only valid if output is reserve

    // success fee is calculated by multiplying the amount by this number and dividing by satoshis (100,000,000), not less than 10x the absolute SUCCESS_FEE
    // failure fee, meaning the valid before block is past but it is not expired is the difference between input and output and must follow those rules
    // it is deducted in the success case from the success fee, so there is no fee beyond the success fee paid
    static const CAmount SUCCESS_FEE = 10000;
    static const CAmount MIN_SUCCESS_FEE = 50000;
    static const CAmount MIN_PARTIAL = 10000000;        // making partial fill minimum the number at which minimum fee meets standard percent fee,
    static const CAmount MIN_NET_CONVERSION = 10000000; // minimum conversion for input
    static const int INTERPOLATE_ROUNDS = 4;            // we ensure that there is no peverse motive to partially fill in order to increase fees
    static const CAmount FILL_OR_KILL_FEE = 10000;
    static const CAmount SATOSHIDEN = 100000000;

    CAmount nLimit;                         // lowest or highest price to sell or buy coin output, may fail if including this tx in block makes price out of range
    uint32_t nValidBefore;                  // if not filled on or after this block, can mine tx, but is spendable to refund input

    CReserveExchange(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveExchange() : CReserveOutput(), nLimit(0), nValidBefore(0) { }

    CReserveExchange(uint32_t Flags, CAmount amountIn, CAmount Limit=0, uint32_t ValidBefore=0) : 
        CReserveOutput(Flags, amountIn), nLimit(Limit), nValidBefore(ValidBefore) { }

    CReserveExchange(const CTransaction &tx, bool validate=false);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CReserveOutput *)this);
        READWRITE(VARINT(nLimit));
        READWRITE(nValidBefore);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        // this needs an actual check
        return CReserveOutput::IsValid();
    }

    UniValue ToUniValue() const;

    bool IsExpired(int32_t height)
    {
        return height >= nValidBefore;
    }
};

struct CReserveExchangeData
{
    const CTransaction *ptx;
    uint32_t flags;
    CAmount nLimit;
    CAmount nFee;
    CAmount nInputValue;
};

class CReserveTransactionDescriptor
{
public:
    static const uint32_t IS_VALID=1;           // known to be valid
    static const uint32_t IS_REJECT=2;          // if set, tx is known to be invalid
    static const uint32_t IS_RESERVE=4;         // if set, this transaction affects reserves and/or price if mined
    static const uint32_t IS_RESERVEEXCHANGE=8; // is this a reserve/exchange transaction?
    static const uint32_t IS_LIMIT=0x10;        // if reserve exchange, is it a limit order?
    static const uint32_t IS_FILLORKILL=0x20;   // If set, this can expire
    static const uint32_t IS_FILLORKILLFAIL=0x40; // If set, this is an expired fill or kill in a valid tx
    static const uint32_t IS_IMPORT=0x80;       // If set, this is an expired fill or kill in a valid tx
    static const uint32_t IS_EXPORT=0x100;      // If set, this is an expired fill or kill in a valid tx
    static const uint32_t IS_IDENTITY=0x200;    // If set, this is an identity definition or update
    static const uint32_t IS_IDENTITY_DEFINITION=0x400; // If set, this is an identity definition
    static const uint32_t IS_HIGH_FEE=0x800;    // If set, this may have "absurdly high fees"

    const CTransaction *ptx;                    // pointer to the actual transaction if valid
    uint32_t flags;                             // indicates transaction state
    int32_t numBuys = 0;                        // each limit conversion that is valid before a certain block should account for FILL_OR_KILL_FEE
    int32_t numSells = 0;
    int32_t numTransfers = 0;                   // number of transfers, each of which also requires a transfer fee
    int64_t reserveIn = 0;
    int64_t reserveOutConverted = 0;
    int64_t reserveOut = 0;                     // total value of both converting and non-converting reserve outputs
    int64_t nativeIn = 0;
    int64_t nativeOutConverted = 0;
    int64_t nativeOut = 0;
    int64_t nativeConversionFees = 0;           // non-zero only if there is a conversion
    int64_t reserveConversionFees = 0;
    std::vector<std::pair<int, CReserveExchange>> vRex; // index and rehydrated, validated reserve exchange outputs

    CReserveTransactionDescriptor() : 
        flags(0),
        ptx(NULL),
        numBuys(0),                             // each limit conversion that is valid before a certain block should account for FILL_OR_KILL_FEE
        numSells(0),
        numTransfers(0),
        reserveIn(0),
        reserveOutConverted(0),
        reserveOut(0),                          // total of both converting and non-converting outputs
        nativeIn(0),
        nativeOutConverted(0),
        nativeOut(0),
        nativeConversionFees(0),                // non-zero only if there is a conversion, stored vs. calculated to get exact number with each calculated seperately
        reserveConversionFees(0)  {}

    CReserveTransactionDescriptor(const CTransaction &tx, const CCoinsViewCache &view, int32_t nHeight);

    bool IsReject() const { return flags & IS_REJECT; }
    bool IsValid() const { return flags & IS_VALID && !IsReject(); }
    bool IsReserve() const { return IsValid() && flags & IS_RESERVE; }
    bool IsReserveExchange() const { return flags & IS_RESERVEEXCHANGE; }
    bool IsLimit() const { return flags & IS_LIMIT; }
    bool IsFillOrKill() const { return flags & IS_FILLORKILL; }
    bool IsMarket() const { return IsReserveExchange() && !IsLimit(); }
    bool IsFillOrKillFail() const { return flags & IS_FILLORKILLFAIL; }
    bool IsIdentity() const { return flags & IS_IDENTITY; }
    bool IsIdentityDefinition() const { return flags & IS_IDENTITY_DEFINITION; }
    bool IsHighFee() const { return flags & IS_HIGH_FEE; }

    static CAmount CalculateConversionFee(CAmount inputAmount);
    static CAmount CalculateAdditionalConversionFee(CAmount inputAmount);

    CAmount NativeFees() const
    {
        return nativeIn - nativeOut;                                // native out converted does not include conversion
    }

    CAmount ReserveFees() const
    {
        return reserveIn - reserveOut;
    }

    CAmount AllFeesAsNative(const CCurrencyState &currencyState) const;
    CAmount AllFeesAsNative(const CCurrencyState &currencyState, CAmount exchangeRate) const;
    CAmount AllFeesAsReserve(const CCurrencyState &currencyState) const;
    CAmount AllFeesAsReserve(const CCurrencyState &currencyState, CAmount exchangeRate) const;

    void AddReserveOutput(const CReserveOutput &ro)
    {
        flags |= IS_RESERVE;
        if (!(flags & IS_IMPORT))
        {
            reserveOut += ro.nValue;
        }
    }

    // is boolean, since it can fail, which would render the tx invalid
    void AddReserveExchange(const CReserveExchange &rex, int32_t outputIndex, int32_t nHeight);

    void AddReserveTransfer(CReserveTransfer &rt)
    {
        flags |= IS_RESERVE;
        if (!(flags & IS_IMPORT))
        {
            reserveOut += rt.nValue + rt.nFees;
            numTransfers++;
        }
    }

    CMutableTransaction &AddConversionInOuts(CMutableTransaction &conversionTx, std::vector<CInputDescriptor> &conversionInputs, CAmount exchangeRate=0, const CCurrencyState *pCurrencyState=NULL) const;
    bool AddReserveTransferImportOutputs(const CPBaaSChainDefinition &chainDef, const std::vector<CBaseChainObject *> &exportObjects, std::vector<CTxOut> &vOutputs);
};

// import transactions from another chain
// an import transaction on a fractional reserve chain will have an instant spend input of EVAL_CHAIN_IMPORT from the coinbase, 
// which provides for import of a reserve currency or cross-chain token, or auto-conversion to the native currency.
class CCrossChainImport
{
public:
    uint160 chainID;                                            // usually the reserve currency, but here for generality
    CAmount nValue;                                             // total amount of coins imported from chain with or without conversion, including fees

    CCrossChainImport() : nValue(0) { }
    CCrossChainImport(const uint160 &cID, const CAmount value) : chainID(cID), nValue(value) { }

    CCrossChainImport(const std::vector<unsigned char> &asVector)
    {
        ::FromVector(asVector, *this);
    }

    CCrossChainImport(const CTransaction &tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainID);
        READWRITE(VARINT(nValue));
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        return !chainID.IsNull();
    }

    UniValue ToUniValue() const;
};

// describes an entire output that will be realized on a target chain. target is specified as part of an aggregated transaction.
class CCrossChainExport
{
public:
    static const int MIN_BLOCKS = 10;
    static const int MIN_INPUTS = 10;
    static const int MAX_EXPORT_INPUTS = 50;
    uint160 chainID;                                // target chain ID
    int32_t numInputs;                              // number of inputs aggregated to calculate the fee percentage
    CAmount totalAmount;                            // total amount of inputs, not including fees
    CAmount totalFees;                              // total amount of fees to split between miner on exporting chain and importing chain

    CCrossChainExport() : numInputs(0), totalAmount(0), totalFees(0) {}

    CCrossChainExport(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CCrossChainExport(uint160 cID, int32_t numin, CAmount value, CAmount fees) : chainID(cID), numInputs(numin), totalAmount(value), totalFees(fees) {}

    CCrossChainExport(const CTransaction &tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainID);
        READWRITE(numInputs);
        READWRITE(VARINT(totalAmount));
        READWRITE(VARINT(totalFees));
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid() const
    {
        return !chainID.IsNull();
    }

    CAmount CalculateExportFee() const;

    CAmount CalculateImportFee() const
    {
        return totalFees - CalculateExportFee();
    }
    
    UniValue ToUniValue() const;
};

class CCurrencyState
{
public:
    static const uint32_t VALID = 1;
    static const uint32_t ISRESERVE = 2;
    static const int32_t MIN_RESERVE_RATIO = 1000000;       // we will not start a chain with this reserve ratio
    static const int32_t SHUTDOWN_RESERVE_RATIO = 500000;   // if we hit this reserve ratio through selling and emission, initiate chain shutdown
    static const int32_t CONVERSION_TX_SIZE_MIN = 1024;
    static const int32_t CONVERSION_TX_SIZE_PEROUTPUT = 200;

    uint32_t flags;         // currency flags (valid, reserve currency, etc.)

    int32_t InitialRatio;   // starting point reserve percent for initial currency and emission, over SATOSHIs
    CAmount InitialSupply;  // initial supply as premine + pre-converted coins, this is used to establish the value at the initial ratio as well
    CAmount Emitted;        // unlike other supply variations, emitted coins reduce the reserve ratio and are used to calculate current ratio
    CAmount Supply;         // current supply - total of initial and all emitted coins
    CAmount Reserve;        // current reserve controlled by fractional chain - only non-zero for reserve currencies

    //std::vector<CAmount> Reserves; // reserve currencies amounts controlled by this fractional chain - only present for reserve currencies, currency IDs are in chain definition

    CCurrencyState() : flags(0), InitialSupply(0), Emitted(0), Supply(0), Reserve(0) {}

    CCurrencyState(int32_t initialRatio, CAmount supply, CAmount initialSupply, CAmount emitted, CAmount reserve, uint32_t Flags=VALID) : 
        flags(Flags), Supply(supply), InitialSupply(initialSupply), Emitted(emitted), Reserve(reserve)
    {
        if (initialRatio > CReserveExchange::SATOSHIDEN)
        {
            initialRatio = CReserveExchange::SATOSHIDEN;
        }
        else if (initialRatio < MIN_RESERVE_RATIO)
        {
            initialRatio = MIN_RESERVE_RATIO;
        }
        InitialRatio = initialRatio;
    }

    CCurrencyState(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CCurrencyState(const UniValue &uni);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(flags);
        READWRITE(VARINT(InitialRatio));
        READWRITE(VARINT(InitialSupply));
        READWRITE(VARINT(Emitted));
        READWRITE(VARINT(Supply));
        READWRITE(VARINT(Reserve));        
    }

    std::vector<unsigned char> AsVector() const
    {
        return ::AsVector(*this);
    }

    // this should be done no more that once to prepare a currency state to be moved to the next state
    // emission occurs for a block before any conversion or exchange and that impact on the currency state is calculated
    CCurrencyState &UpdateWithEmission(CAmount emitted);

    cpp_dec_float_50 GetReserveRatio() const
    {
        return cpp_dec_float_50(std::to_string(InitialRatio)) / cpp_dec_float_50("100000000");
    }

    template<typename cpp_dec_float_type>
    static bool to_int64(const cpp_dec_float_type &input, int64_t &outval)
    {
        std::stringstream ss(input.str(0));
        try
        {
            ss >> outval;
            return true;
        }
        catch(const std::exception& e)
        {
            return false;
        }
    }

    CAmount PriceInReserve() const
    {
        if (Supply == 0 || InitialRatio == 0)
        {
            return InitialRatio;
        }
        arith_uint256 supply(Supply);
        arith_uint256 reserve(Reserve);
        arith_uint256 ratio(InitialRatio);

        return ((reserve * arith_uint256(CReserveExchange::SATOSHIDEN) * arith_uint256(CReserveExchange::SATOSHIDEN)) / (supply * ratio)).GetLow64();
    }

    // return the current price of the fractional reserve in the reserve currency in Satoshis
    cpp_dec_float_50 GetPriceInReserve() const
    {
        return cpp_dec_float_50(PriceInReserve());
    }

    // This can handle multiple aggregated, bidirectional conversions in one block of transactions. To determine the conversion price, it 
    // takes both input amounts of the reserve and the fractional currency to merge the conversion into one calculation
    // with the same price for all transactions in the block. It returns the newly calculated conversion price of the fractional 
    // reserve in the reserve currency.
    CAmount ConvertAmounts(CAmount inputReserve, CAmount inputFractional, CCurrencyState &newState) const;

    // convert amounts for multi-reserve fractional reserve currencies
    std::vector<CAmount> ConvertAmounts(const std::vector<CAmount> &inputReserve, CAmount inputFractional, CCurrencyState &newState) const;

    CAmount CalculateConversionFee(CAmount inputAmount, bool convertToNative = false) const;
    CAmount ReserveFeeToNative(CAmount inputAmount, CAmount outputAmount) const;

    CAmount ReserveToNative(CAmount reserveAmount) const;
    static CAmount ReserveToNative(CAmount reserveAmount, CAmount exchangeRate);

    CAmount NativeToReserve(CAmount nativeAmount) const
    {
        static arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
        arith_uint256 bigAmount(nativeAmount);
        arith_uint256 price = arith_uint256(PriceInReserve());
        return ((bigAmount * arith_uint256(price)) / bigSatoshi).GetLow64();
    }

    static CAmount NativeToReserve(CAmount nativeAmount, CAmount exchangeRate)
    {
        static arith_uint256 bigSatoshi(CReserveExchange::SATOSHIDEN);
        arith_uint256 bigAmount(nativeAmount);
        return ((bigAmount * arith_uint256(exchangeRate)) / bigSatoshi).GetLow64();
    }

    UniValue ToUniValue() const;

    bool IsValid() const
    {
        return flags & CCurrencyState::VALID;
    }

    bool IsReserve() const
    {
        return flags & CCurrencyState::ISRESERVE;
    }
};

class CCoinbaseCurrencyState : public CCurrencyState
{
public:
    CAmount ReserveIn;      // reserve currency converted to native
    CAmount NativeIn;       // native currency converted to reserve
    CReserveOutput ReserveOut; // output can have both normal and reserve output value, if non-0, this is spent by the required conversion output transactions
    CAmount ConversionPrice;// calculated price in reserve for all conversions * 100000000
    CAmount Fees;           // fee values in native (or reserve if specified) coins for reserve transaction fees for the block
    CAmount ConversionFees; // total of only conversion fees, which will accrue to the conversion transaction

    CCoinbaseCurrencyState() : ReserveIn(0), NativeIn(0), ConversionPrice(0), Fees(0), ConversionFees(0) {}

    CCoinbaseCurrencyState(int32_t initialRatio, CAmount supply, CAmount initialSupply, CAmount emitted, CAmount reserve,
                             CAmount reserveIn, CAmount nativeIn, CReserveOutput reserveOut, CAmount conversionPrice, CAmount fees, CAmount conversionFees) : 
        CCurrencyState(initialRatio, supply, initialSupply, emitted, reserve), ReserveIn(reserveIn), NativeIn(nativeIn), ReserveOut(reserveOut), ConversionPrice(conversionPrice), Fees(fees), ConversionFees(conversionFees) { }

    CCoinbaseCurrencyState(const CCurrencyState &currencyState, CAmount reserveIn, CAmount nativeIn, const CReserveOutput &reserveOut, CAmount conversionPrice, CAmount fees, CAmount conversionFees) : 
        CCurrencyState(currencyState), ReserveIn(reserveIn), NativeIn(nativeIn), ReserveOut(reserveOut), ConversionPrice(conversionPrice), Fees(fees), ConversionFees(conversionFees) { }

    CCoinbaseCurrencyState(const UniValue &uni);

    CCoinbaseCurrencyState(const std::vector<unsigned char> asVector)
    {
        ::FromVector(asVector, *this);
    }

    CCoinbaseCurrencyState(const CTransaction &tx, int *pOutIdx=NULL);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CCurrencyState *)this);
        READWRITE(VARINT(ReserveIn));
        READWRITE(VARINT(NativeIn));
        READWRITE(ReserveOut);
        READWRITE(VARINT(ConversionPrice));
        READWRITE(VARINT(Fees));
        READWRITE(VARINT(ConversionFees));
    }

    std::vector<unsigned char> AsVector() const
    {
        return ::AsVector(*this);
    }

    UniValue ToUniValue() const;

    CCoinbaseCurrencyState MatchOrders(const std::vector<const CTransaction *> &orders, 
                                        std::vector<CReserveTransactionDescriptor> &reserveFills, 
                                        std::vector<const CTransaction *> &expiredFillOrKills, 
                                        std::vector<const CTransaction *> &noFills, 
                                        std::vector<const CTransaction *> &rejects, 
                                        CAmount &price, int32_t height, std::vector<CInputDescriptor> &conversionInputs, 
                                        int64_t maxSerializedSize=LONG_MAX, int64_t *ptotalSerializeSize=NULL, CMutableTransaction *pConversionTx=NULL,
                                        bool feesAsReserve=false) const;
};

#endif // PBAAS_RESERVES_H
