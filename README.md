# CHIMERA v4.14.0 - Unified Engine

## Overview

Clean, unified HFT trading system with exactly 3 symbols.

**SYMBOLS: XAUUSD, NAS100, US30 ONLY**

- NO CRYPTO
- NO FOREX
- Single binary architecture

## NAS100 Settings (BACKTEST-LOCKED)

| Parameter | Value |
|-----------|-------|
| OR Window | 15 min |
| OR Min Range | 25 pts |
| OR Max Range | 120 pts |
| Sweep Distance | 35 pts |
| Reversion Trigger | 18 pts |
| Size Multiplier | 2.0x |

## Build

```bash
mkdir build && cd build
cmake ..
make
./chimera
```

## Architecture

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

## Engines

### NAS100
- **OpeningRangeGate**: First 15 min of NY session, validates 25-120pt range
- **SweepReversion**: Fades 35pt sweeps beyond OR, entry at 18pt revert

### US30
- **OpeningRangeGate**: Same logic, 40-200pt range
- **SweepReversion**: 60pt sweep, 30pt revert trigger

### XAUUSD
- **MeanRevert**: Fades velocity spikes in inventory correction regime
- **StopFade**: Fades exhausted stop sweeps with stall confirmation
- **AcceptanceBO**: Time-in-zone breakouts (1s hold minimum)

## Risk

- Daily loss limit: $500
- Max trades per symbol: 5/day
- Per-symbol risk allocation enforced

## Sessions

- NY Session: 13:30-20:00 UTC
- Gold Optimal: 13:30-16:00 UTC

## Dashboard

Open `chimera_dashboard.html` in browser.

## VPS Deployment

```bash
# Build on Mac with Zig for Windows
zig cc -target x86_64-windows-gnu ...

# Copy to VPS via RDP to C:\Chimera\

# On VPS (WSL):
cp /mnt/c/Chimera/Chimera_v4.14.0.zip ~/
cd ~ && unzip Chimera_v4.14.0.zip
cd Chimera_v4.14.0/build
cmake .. && make
./chimera
```

## Files

- `src/main.cpp` - Entry point
- `include/engines/ChimeraUnifiedEngine.hpp` - All engines
- `chimera_dashboard.html` - Web dashboard
- `cfd_engine/include/fix/` - FIX protocol stack

## Author

Jo - Quantitative Trader  
Auckland, New Zealand
