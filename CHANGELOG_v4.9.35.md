# Chimera v4.9.35 - Open API Integration Fixes

## Release Date: 2025-01-04

## Summary
Fixed compile-time switch between FIX 4.4 and cTrader Open API protocols. All code paths now properly use conditional compilation, eliminating hardcoded FIX references that would break OpenAPI builds.

## Changes

### CMakeLists.txt
- Added `USE_OPENAPI` cmake option (default: OFF)
- Added `-DUSE_OPENAPI` compile definition when enabled
- Updated project version to 4.9.35
- Added status messages showing which protocol is selected

### cfd_engine/include/CfdEngine.hpp
- Changed namespace aliases to include `ClientConfig` type alias
- Replaced hardcoded `using Chimera::CTraderFIXClient` with `using CTraderClient`
- Replaced hardcoded `using Chimera::FIXConfig` with `using ClientConfig`
- Added `setConfig()` method as generic config setter
- Kept `setFIXConfig()` as legacy alias for backward compatibility
- Changed member variables:
  - `FIXConfig fixConfig_` → `ClientConfig clientConfig_`
  - `CTraderFIXClient fixClient_` → `CTraderClient client_`
- Updated all internal references to use new member names

### cfd_engine/include/CTraderTypes.hpp
- Added `OrderSide` namespace with `Buy` ('1') and `Sell` ('2') constants
- Added `FIXSide` as namespace alias to `OrderSide` for backward compatibility
- These constants are now available regardless of which client is compiled

### src/main_dual.cpp
- Added `#ifdef USE_OPENAPI` block for config type selection
- Added explicit include for `CTraderTypes.hpp` when using OpenAPI
- Changed config instantiation to use conditional type
- Updated to use `setConfig()` instead of `setFIXConfig()`
- Added protocol announcement message

### src/main_triple.cpp
- Same changes as main_dual.cpp

## Build Commands

```bash
# Default FIX build (unchanged behavior)
cmake .. && make

# Open API build (new)
cmake .. -DUSE_OPENAPI=ON && make
```

## Files Modified
1. `CMakeLists.txt`
2. `cfd_engine/include/CfdEngine.hpp`
3. `cfd_engine/include/CTraderTypes.hpp`
4. `src/main_dual.cpp`
5. `src/main_triple.cpp`

## Files Preserved (No Changes)
- All 9 FIX files in `cfd_engine/include/fix/`
- `cfd_engine/include/openapi/CTraderOpenAPIClient.hpp`
- All other source files

## Testing Notes
- FIX path: Unchanged behavior, should pass all existing tests
- OpenAPI path: Requires OAuth token from browser flow before testing

## Next Steps When Open API Approved
1. Get OAuth token via browser flow
2. Add `access_token` and `account_id` to config.ini
3. Build with `-DUSE_OPENAPI=ON`
4. Test on demo.ctraderapi.com:5035
5. Go live on live.ctraderapi.com:5035
