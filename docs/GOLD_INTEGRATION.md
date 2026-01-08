# Gold Quad Engine v5.9.1 - Chimera Integration
## XAUUSD Microstructure Trading System

---

## QUICK START

### 1. Include Header
```cpp
#include "gold_quad_engine.hpp"
```

### 2. Initialize Engine
```cpp
gold::GoldQuadEngine goldEngine;

// Set velocity threshold (calculate from historical data or use default)
goldEngine.setVelocityThreshold(1.5);  // Adjust based on your data
```

### 3. Feed Ticks
```cpp
void CfdEngine::onTick(const TickFull& tick) {
    if (tick.symbol == "XAUUSD") {
        goldEngine.onTick(tick.timestamp_ms, tick.bid, tick.ask);
        
        // Check for signals
        processGoldSignals();
    }
}
```

### 4. Process Signals
```cpp
void CfdEngine::processGoldSignals() {
    // SRM Signal (micro momentum)
    if (goldEngine.hasSRMPosition()) {
        auto sig = goldEngine.getSRMSignal();
        if (sig.active && !hasOpenPosition("XAUUSD", gold::EngineId::SRM)) {
            executeGoldTrade(sig);
        }
    }
    
    // GRI Signal (macro momentum)
    if (goldEngine.hasGRIPosition()) {
        auto sig = goldEngine.getGRISignal();
        if (sig.active && !hasOpenPosition("XAUUSD", gold::EngineId::GRI)) {
            executeGoldTrade(sig);
        }
    }
    
    // MR and SF signals similarly...
}
```

---

## ENGINE ARCHITECTURE

```
┌──────────────────────────────────────────────────────────────┐
│  Engine    │  Role                    │  Frequency  │ Weight │
├──────────────────────────────────────────────────────────────┤
│  MR        │  Liquidity harvesting    │  Very High  │  0.75x │
│  SF        │  Stop inefficiency       │  Medium     │  1.25x │
│  SRM       │  Micro momentum (cash)   │  Low        │  2.25x │
│  GRI       │  Macro momentum (rare)   │  Very Low   │  3.00x │
└──────────────────────────────────────────────────────────────┘
```

### Role Definitions:
- **MR**: High-frequency mean reversion after velocity spikes in INV_CORR state
- **SF**: Fades stop hunts after sweep detection in STOP_SWEEP state  
- **SRM**: Trades WITH momentum after sweep holds (the "salary" engine)
- **GRI**: Trades rare macro sweeps >$1.50 (the "bonus" engine)

---

## PARAMETERS (LOCKED)

### SRM Parameters
| Parameter | Value | Description |
|-----------|-------|-------------|
| MIN_SWEEP | $0.60 | Minimum sweep to detect |
| HOLD_WINDOW | 400ms | Time to confirm hold |
| HOLD_MAX_RANGE | $0.25 | Max range during hold |
| TP | $2.00 | Take profit |
| SL | $0.70 | Stop loss |
| SIZE_MULT | 1.0-1.8x | Entry-time scaling |

### GRI Parameters
| Parameter | Value | Description |
|-----------|-------|-------------|
| MIN_SWEEP | $1.50 | Macro sweep threshold |
| SWEEP_WINDOW | 600ms | Max time for sweep |
| VEL_PCTL | 95th | Velocity percentile filter |
| PRE_RANGE_MIN | $0.80 | NO compression allowed |
| SL | $1.20 | Stop loss |
| TP_PARTIAL | $2.00 | Partial take profit |
| RUNNER_TRAIL | $0.80 | Trailing for runner |

---

## RISK CONTROLS

### Daily Caps
```cpp
SF_DAILY_CAP = 3      // Max SF trades per day
SRM_DAILY_CAP = 2     // Max SRM trades per day
GRI_DAILY_CAP = 1     // Max GRI trades per day
```

### Loss Limits
```cpp
DAILY_LOSS_CAP = $2,500      // Global daily stop
SRM_DAILY_LOSS_CAP = $1,200  // SRM-specific daily stop
```

When SRM hits its daily loss cap, it disables for the day while MR/SF continue.

---

## SESSION FILTERS

### SRM Blocked Hours (UTC)
- 4, 5 (Asia dead)
- 10 (London lunch)
- 21, 22 (NY close)

### GRI Allowed Hours (UTC)
- 7-10 (London open)
- 13-16 (NY overlap)
- 19-20 (NY session)

---

## MARKET STATE CLASSIFICATION

The engine classifies market into 4 states:

| State | Condition | Active Engines |
|-------|-----------|----------------|
| DEAD | Range < $0.30, Spread > $0.80 | None |
| EXPANSION | Range > $5.00 | Caution |
| INV_CORR | Default liquid | MR |
| STOP_SWEEP | Spread > $0.70 | SF, SRM, GRI |

---

## SIGNAL STRUCTURE

```cpp
struct TradeSignal {
    bool active;           // Signal is live
    EngineId engine;       // MR, SF, SRM, or GRI
    int direction;         // 1 = long, -1 = short
    double entry_price;    // Entry level
    double tp_price;       // Take profit
    double sl_price;       // Stop loss
    double size_mult;      // Size multiplier (SRM only)
    int64_t entry_ts;      // Entry timestamp
};
```

---

## BACKTEST VALIDATION

Run the Python backtester to validate:

```bash
python3 gold_backtest_fast.py /path/to/xauusd/ticks/ --full
```

### Expected Results (from validation):
- **Combined PnL**: ~$45,000
- **DD 99th**: ~$11,700
- **SRM WFA**: 11/15 windows positive
- **GRI**: Adds clean expectancy without destabilizing

---

## IMPORTANT NOTES

1. **GRI is NOT a tail engine** - it's macro momentum that wins ~35% of the time
2. **SRM is the cash engine** - consistent micro momentum  
3. **Do NOT modify parameters** without full WFA + MC revalidation
4. **Velocity threshold** should be calibrated from your specific data feed

---

## FILES INCLUDED

| File | Location | Purpose |
|------|----------|---------|
| `gold_quad_engine.hpp` | `cfd_engine/include/` | C++ engine |
| `gold_backtest_fast.py` | `gold/` | Python backtester |
| `GOLD_INTEGRATION.md` | `docs/` | This file |
