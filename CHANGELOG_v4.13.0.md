# CHANGELOG v4.13.0 - Gold Live Engine

**Release Date:** 2025-01-08

## Overview

Added standalone gold tick-level trading engine for cTrader OpenAPI, keeping
the existing bar-level campaign engine isolated. This release maintains clean
separation between:
- `chimera` (main binary) - NAS100 + US30 E2 VWAP trading
- `gold_live` (new) - XAUUSD tick-level microstructure trading

## New Features

### Gold Live Engine (`gold_live` target)

Standalone tick-level gold trading engine with four sub-engines:

1. **MeanRevertEngine**
   - Trades inventory correction regimes
   - Fades velocity spikes ≥$0.25/tick
   - Size: 0.75x base
   - Cooldown: 3 seconds

2. **StopFadeEngine**  
   - Fades exhausted stop sweeps
   - Requires ≥$0.50 sweep + stall pattern
   - Stall detection: 400ms window, <$0.10 range
   - Size: 1.25x base

3. **AcceptanceBOEngineV2**
   - Time-in-zone acceptance breakouts
   - Requires 1+ second hold within $0.25 zone
   - Size: 2.25x base

4. **CampaignDetector**
   - Detects institutional campaigns (1+ hour expansions)
   - Informational only (no trade signals)
   - Provides context for other engines

### Regime Classification

- **DEAD**: Low activity, wide spreads (>$0.80)
- **INVENTORY_CORRECTION**: Tight spreads (<$0.20)
- **STOP_SWEEP**: Spread blowout (>$1.00)
- **EXPANSION**: Large moves (>$0.50/tick)
- **RANGE_VOLATILE**: Default state

### Risk Management (Hardcoded)

- Daily loss cap: $500
- Max daily trades: 10
- Base size: 0.01 lots (configurable via `Config::BASE_SIZE_LOTS`)

## Build Instructions

### Main Binary (default)
```bash
cd build && cmake .. && make chimera
```

### Gold Live (requires Asio + nlohmann_json)
```bash
cd build && cmake -DBUILD_GOLD_LIVE=ON .. && make gold_live
```

### Test Mode (synthetic ticks)
```bash
cmake -DBUILD_GOLD_LIVE=ON -DGOLD_LIVE_TEST_MODE=ON .. && make gold_live
```

## Configuration Required

Before production use of `gold_live`, edit `src/chimera_gold_live.cpp`:

```cpp
namespace Config {
    static const char* CTRADER_CLIENT_ID     = "YOUR_CLIENT_ID";
    static const char* CTRADER_CLIENT_SECRET = "YOUR_SECRET";
    static const char* CTRADER_ACCESS_TOKEN  = "YOUR_TOKEN";
    static const int64_t CTRADER_ACCOUNT_ID  = 12345;
    static const int64_t SYMBOL_ID_XAUUSD    = 67890;
}
```

## Architecture Notes

### No Duplication

The codebase now has three distinct gold components:

1. **gold_quad_engine.hpp** (`cfd_engine/include/`)
   - Original tick-level quad engine
   - Used by CfdEngine for integrated gold trading
   - NOT used by standalone gold_live

2. **GoldEngine_v5_2.hpp** (`include/engines/`)
   - Bar-level campaign/position management
   - Uses M5/H1 bar inputs
   - Isolated (not actively trading)

3. **chimera_gold_live.cpp** (`src/`)
   - NEW: Standalone tick-level engine
   - Independent cTrader connection
   - Separate risk management

### Compilation

- Default build: Only `chimera` target (fast)
- No 4x compilation issue (headers properly isolated)
- Heavy headers remain in main_microlive.cpp only

## Files Changed

- `CMakeLists.txt` - Added gold_live target, version bump
- `src/main_microlive.cpp` - Version strings updated
- `src/chimera_gold_live.cpp` - NEW: Standalone gold engine

## Migration Notes

- No changes to existing functionality
- Main chimera binary unchanged in behavior
- Gold trading remains ISOLATED in main binary
- New gold_live binary is opt-in via cmake flag

## Dependencies (gold_live only)

- standalone Asio (header-only)
- nlohmann_json
- OpenSSL
- pthreads
