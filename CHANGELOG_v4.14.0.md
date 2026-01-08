# CHANGELOG v4.14.0 - Unified Engine

**Release Date:** 2025-01-08

## Overview

Complete architecture overhaul: unified CRTP-based engine with exactly 3 symbols.

**SYMBOLS: XAUUSD, NAS100, US30 ONLY**

- NO CRYPTO (tombstoned)
- NO FOREX (removed)
- Clean, single-binary architecture

## NAS100 Settings (BACKTEST-LOCKED)

| Parameter | Value |
|-----------|-------|
| OR Window | 15 min |
| OR Min Range | 25 pts |
| OR Max Range | 120 pts |
| Sweep Distance | 35 pts |
| Reversion Trigger | 18 pts |
| Size Multiplier | 2.0x |

**DO NOT MODIFY WITHOUT FULL REVALIDATION**

## Architecture

### Unified Engine (`ChimeraUnifiedEngine.hpp`)

Single header containing all engines with CRTP zero-overhead polymorphism:

```
ChimeraUnifiedSystem
├── NAS100System
│   ├── NAS100OpeningRangeGate
│   └── NAS100SweepReversion
├── US30System
│   ├── US30OpeningRangeGate
│   └── US30SweepReversion
└── XAUUSDSystem
    ├── RegimeClassifier
    ├── XAUUSDMeanRevert
    ├── XAUUSDStopFade
    └── XAUUSDAcceptanceBO
```

### Single Binary

```bash
mkdir build && cd build && cmake .. && make
./chimera
```

## Engines

### NAS100

1. **NAS100OpeningRangeGate**
   - Tracks first 15 min of NY session
   - Validates range: 25-120 pts
   - Gates sweep reversion entries

2. **NAS100SweepReversion**
   - Requires valid opening range
   - Sweep trigger: 35 pts beyond OR
   - Reversion entry: 18 pts back
   - Size: 2.0x base

### US30

1. **US30OpeningRangeGate**
   - Same logic, adjusted params
   - Range: 40-200 pts (larger moves)

2. **US30SweepReversion**
   - Sweep: 60 pts
   - Revert: 30 pts
   - Size: 1.5x base

### XAUUSD

1. **XAUUSDMeanRevert**
   - Trades inventory correction regime
   - Fades velocity spikes ≥$0.25/tick
   - Size: 0.75x, Cooldown: 3s

2. **XAUUSDStopFade**
   - Fades exhausted stop sweeps
   - Requires stall pattern (400ms, <$0.10)
   - Size: 1.25x

3. **XAUUSDAcceptanceBO**
   - Time-in-zone acceptance (1s minimum)
   - Zone tolerance: $0.25
   - Size: 2.25x

## Risk Management

- Daily loss limit: $500
- Max trades per symbol: 5/day
- Position size limits enforced

## Session Gates

- NY Session: 13:30-20:00 UTC
- Gold Optimal: 13:30-16:00 UTC (London/NY overlap)

## Files Changed

### New
- `include/engines/ChimeraUnifiedEngine.hpp` - Complete unified engine
- `src/main.cpp` - Clean entry point
- `chimera_dashboard.html` - 3-symbol dashboard

### Removed (Tombstoned)
- All crypto references
- All forex references
- Old multi-file engine structure

## Build

```bash
mkdir build && cd build
cmake ..
make
./chimera
```

## Dashboard

Open `chimera_dashboard.html` in browser.
Connect to WebSocket at `ws://VPS_IP:7777`

## Migration Notes

- Single binary replaces all previous targets
- Old main_microlive.cpp preserved but not used
- FIX client unchanged
- Config via code (will add config.ini support)
