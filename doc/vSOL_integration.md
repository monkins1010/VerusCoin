# vSOL Integration Documentation

## Overview
This document outlines the integration of the new gateway currency `vSOL` (Verus-Solana Bridge) into the Verus ecosystem. vSOL will operate alongside existing gateway currencies like `vETH`, providing seamless interoperability with the Solana blockchain.

## Integration Goals
- Implement vSOL as a fully functional gateway currency
- Add Solana-specific proof protocol support (`PROOF_SOLNOTARIZATION`)
- Create Solana-compatible transfer destination types
- Enable multi-gateway architecture that scales beyond ETH/SOL

## Completed Implementation

### 1. Gateway Currency Support
- ✅ Added `CSolGateway` class in `src/pbaas/pbaas.h`
- ✅ Implemented `CSolGateway::FeeCurrencies()` and `CSolGateway::GatewayID()` methods
- ✅ Created `GetSupportedGateways()` function returning set of supported gateway IDs (vETH, vSOL)
- ✅ Replaced hardcoded vETH-only checks with dynamic gateway validation

### 2. Proof Protocol Enhancement
- ✅ Added `PROOF_SOLNOTARIZATION = 4` to `EProofProtocol` enum in `src/pbaas/crosschainrpc.h`
- ✅ Updated validation logic to accept both `PROOF_ETHNOTARIZATION` and `PROOF_SOLNOTARIZATION`
- ✅ Modified mapped currency validation in `src/rpc/pbaasrpc.cpp` lines ~13012 and ~13030

### 3. RPC Function Updates
- ✅ Updated `definecurrency` function to support multiple gateways
- ✅ Modified gateway validation from hardcoded ETH check to dynamic set lookup
- ✅ Enhanced error messages to reflect multi-gateway support

### 4. Help Text and Documentation
- ✅ Updated RPC help text to mention both Ethereum (protocol 3) and Solana (protocol 4)
- ✅ Modified error messages from "Ethereum is the only gateway supported" to "Unsupported gateway. Supported gateways: vETH, vSOL"

## TODO: Areas Requiring Further Investigation

### Critical ETH-Specific Code Patterns to Address

#### 1. Search for `ETH_OBJECT` References
```bash
grep -r "ETH_OBJECT" src/
```
- Review all ETH_OBJECT usage for Solana equivalents
- Implement `SOL_OBJECT` types if needed
- Create generic gateway object handling

#### 2. `PROOF_ETHNOTARIZATION` Hardcoded References
```bash
grep -r "PROOF_ETHNOTARIZATION" src/
```
- Located in: `src/rpc/client.cpp`, `src/pbaas/crosschainrpc.cpp`
- **Action**: Replace hardcoded checks with gateway-agnostic validation
- **Pattern**: `if (protocol == PROOF_ETHNOTARIZATION)` → `if (IsGatewayProtocol(protocol))`

#### 3. `PBAAS_LARGE_ETH_PROOF_ACTIVATION` Constants
```bash
grep -r "PBAAS_LARGE_ETH_PROOF_ACTIVATION" src/
```
- Found in: `src/pbaas/crosschainrpc.h:32`
- **Action**: Determine if Solana needs equivalent activation constant
- **Consider**: `PBAAS_LARGE_SOL_PROOF_ACTIVATION` or generic `PBAAS_LARGE_GATEWAY_PROOF_ACTIVATION`

#### 4. Ethereum-Specific Function Names
```bash
grep -r "ConfigureEthBridge\|EthGateway\|ETH.*Gateway" src/
```
- **Updated**: `ConfigureEthBridge()` → `ConfigureGatewayBridges()`
- **Action**: Verify all ETH-specific function names are generalized

#### 5. Transfer Destination Types
- **TODO**: Implement `DEST_SOL` transfer destination type
- **Current**: Only `DEST_ETH = 9` exists for Ethereum transfers
- **Pattern**: Add `DEST_SOL = 12` and update validation logic

### 6. Gateway-Specific Constants and Limits
```cpp
// Current ETH-specific limits that may need SOL equivalents:
MAX_ETH_CURRENCY_DEFINITION_EXPORTS_PER_BLOCK = 1
MAX_ETH_IDENTITY_DEFINITION_EXPORTS_PER_BLOCK = 0  
MAX_ETH_TRANSFER_EXPORTS_PER_BLOCK = 50
MAX_ETH_TRANSFER_EXPORTS_SIZE_PER_BLOCK = 50000
```
- **Action**: Implement Solana-specific limits or make them configurable per gateway

### 7. Address Encoding/Decoding
- **Current**: `DecodeEthDestination()` and `EncodeEthDestination()` for Ethereum addresses
- **TODO**: Implement `DecodeSolDestination()` and `EncodeSolDestination()` for Solana addresses
- **Consider**: Generic gateway address handling framework

## Scalable Architecture Recommendations

### 1. Gateway Registry Pattern
Instead of expanding if-else chains, implement a gateway registry:

```cpp
class CGatewayRegistry {
public:
    static std::map<uint160, std::unique_ptr<CGateway>> gateways;
    static void RegisterGateway(uint160 id, std::unique_ptr<CGateway> gateway);
    static CGateway* GetGateway(uint160 id);
};
```

### 2. Protocol-Agnostic Validation
Create gateway-agnostic validation functions:

```cpp
bool IsGatewayProtocol(int protocol) {
    return protocol == PROOF_ETHNOTARIZATION || 
           protocol == PROOF_SOLNOTARIZATION;
}

bool IsValidGatewayDestination(int destType, int protocol) {
    // Handle gateway-specific destination validation
}
```

### 3. Configuration-Driven Limits
Replace hardcoded gateway limits with configurable parameters:

```cpp
struct GatewayLimits {
    int32_t maxTransferExports;
    int32_t maxTransferExportSize;
    int32_t maxCurrencyExports;
    int32_t maxIdentityExports;
};

std::map<int, GatewayLimits> GetGatewayLimits();
```

## Testing Requirements

### 1. Integration Tests
- [ ] vSOL gateway creation and validation
- [ ] Cross-chain transfers between Verus and Solana
- [ ] Multi-gateway scenarios (vETH + vSOL simultaneously)

### 2. Regression Tests  
- [ ] Ensure existing vETH functionality remains intact
- [ ] Verify backward compatibility with existing gateway transactions

### 3. Edge Cases
- [ ] Invalid gateway protocol combinations
- [ ] Mixed-protocol transaction handling
- [ ] Gateway-specific address validation

## Future Gateway Support (vPOLY, etc.)

The architecture should support adding new gateways (e.g., vPOLY for Polygon) with minimal code changes:

1. **New Gateway Class**: Inherit from base `CGateway` class
2. **Protocol Constant**: Add new `PROOF_POLYNOTARIZATION` constant  
3. **Registry Entry**: Register new gateway in `GetSupportedGateways()`
4. **Destination Type**: Add new `DEST_POLY` if needed
5. **Limits Configuration**: Define gateway-specific limits

## Implementation Priority

1. **High Priority**: Address `PROOF_ETHNOTARIZATION` hardcoded references
2. **Medium Priority**: Implement `DEST_SOL` transfer destination type
3. **Medium Priority**: Create Solana address encoding/decoding functions
4. **Low Priority**: Implement gateway registry pattern for future scalability

## Notes

- All ETH-specific constants and functions should be generalized to support multiple gateways
- Consider creating a base `CGateway` interface for consistent gateway implementation
- Maintain backward compatibility with existing vETH integrations
- Document any breaking changes for dependent applications

## Additional Tasks

### 8. Multi-Gateway Contract Upgrade Support

**Current Implementation**: The contract upgrade mechanism only supports Ethereum addresses:

```cpp
auto upgradeContractAddress = CTransferDestination::DecodeEthDestination(GetArg("-approvecontractupgrade", ""));
if (!upgradeContractAddress.IsNull())
{
    APPROVE_CONTRACT_UPGRADE = CTransferDestination(CTransferDestination::DEST_ETH, ::AsVector(upgradeContractAddress));
}
```

**Required Enhancement**: Modify the contract upgrade logic to support both Ethereum and Solana gateway addresses:

- **TODO**: Add support for `-approveosolcontractupgrade` parameter for Solana contract addresses
- **TODO**: Allow setting both ETH and SOL contract upgrade addresses simultaneously
- **TODO**: Update `APPROVE_CONTRACT_UPGRADE` to handle multiple gateway types
- **Pattern**: Consider creating `APPROVE_ETH_CONTRACT_UPGRADE` and `APPROVE_SOL_CONTRACT_UPGRADE` variables, or a map structure for multi-gateway support

**Implementation Approach**:
1. Parse both `-approvecontractupgrade` (ETH) and `-approvesolcontractupgrade` (SOL) parameters
2. Store multiple contract upgrade addresses in a gateway-specific data structure
3. Update contract upgrade validation logic to check appropriate gateway type
4. Ensure backward compatibility with existing ETH-only upgrade configurations

**Priority**: Medium - Required for production SOL gateway deployment with upgrade capabilities
