# CHANGELOG v4.6.0 - SPEED-OPTIMISED EXECUTION + ML EXECUTION SYSTEM

## Overview
This release implements:
1. Speed-optimised execution thresholds (US30/SPX500 as Tier 2)
2. **NEW**: Complete ML Execution System (veto + size scaler, not signal generator)

The ML system acts as a quality gate on top of rule-based signals.

---

## ML EXECUTION SYSTEM (NEW)

### Core Architecture
```
Rule Engine proposes trade
  → MLGate.evaluate() 
    → Distribution checks (quantiles)
      → Latency-aware threshold
        → Size scaling
          → ACCEPT or REJECT
```

**ML NEVER FIRES TRADES - IT ONLY VETOES AND SCALES**

### New ML Files

| File | Purpose |
|------|---------|
| `include/ml/MLModel.hpp` | Core types: Regime, Session, Quantiles |
| `include/ml/MLGate.hpp` | Distribution-aware trade filter + NoTradeStreakDetector + SymbolMLEnable |
| `include/ml/MLAttribution.hpp` | Per-trade ML metrics logging |
| `include/ml/MLDriftGuard.hpp` | Drift detection & kill switch (500 sample warmup) |
| `include/ml/GoldPyramiding.hpp` | Gold-specific pyramid logic (venue guard) |
| `include/ml/MLVenueRouter.hpp` | FIX vs CFD routing on tail risk |
| `include/ml/MLMetricsPublisher.hpp` | Live dashboard: health, confidence metrics |
| `ml/build_labels.py` | Create expectancy labels |
| `ml/train_expectancy_models.py` | Regime-specific training |
| `ml/export_features.py` | Binary to CSV conversion |

### MLGate Decision Logic
1. **IQR Check** - Distribution must be meaningful (> session threshold)
2. **Tail Risk** - q10 must not exceed max_tail_loss
3. **Latency-Aware Edge** - Higher latency requires more edge
4. **Latency Hard Block** - Maximum latency per session
5. **Regime-Specific** - DEAD regime requires asymmetric upside
6. **Size Scaling** - Based on expectancy ratio (0.25x to 1.5x)

### Session Thresholds (Locked v4.6.0)
| Session | Hours (UTC) | Min Edge | Max Tail | Max Latency | Max Size |
|---------|-------------|----------|----------|-------------|----------|
| ASIA    | 21:00-07:00 | 1.8      | 1.2      | 120μs       | 0.6x     |
| LONDON  | 07:00-12:30 | 1.3      | 1.5      | 180μs       | 1.0x     |
| NY      | 12:30-21:00 | 1.0      | 2.0      | 250μs       | 1.5x     |

### Gold Pyramiding Rules
**Conditions (ALL must pass):**
- Symbol == XAUUSD
- Regime == BURST  
- Session == NY
- Position profitable (> 0.5)
- q75 expanding (> 0.6)
- Latency < 120μs
- Max 2 pyramid levels

### ML Drift Guard
**Kills trading if:**
- Rolling q10 < -2.0 (tail risk explosion)
- IQR expands > 3x baseline (model confused)

**Throttles if:**
- Rolling q50 < 0.2 (edge eroding)

**500 Sample Warmup**: No kill/throttle decisions until warmup complete.

### No-Trade Streak Detector (Diagnostic)
Warns when ML rejects >95% of candidates in 30-minute window.
Indicates possible:
- Regime misclassification
- Feature drift
- Broken upstream signal

**Warning only** - does not take action.

### Symbol-Level ML Enable
Per-symbol ML control for:
- A/B testing (ML on vs ML off)
- Isolating problem symbols
- Gradual rollout

```cpp
auto& ml_enable = getSymbolMLEnable();
ml_enable.disable("XAUUSD");  // Disable ML for gold
ml_enable.enable("XAUUSD");   // Re-enable
ml_enable.printStatus();      // Show all symbol states
```

### ML Attribution Logging
Per-trade logs include:
- All quantiles (q10, q25, q50, q75, q90)
- Regime, Session
- Latency, Size scale
- Entry/Exit prices
- Realized PnL, MFE, MAE, Hold time

### ML Training Workflow
```bash
# 1. Run Chimera to collect data
./chimera

# 2. Build training labels
python3 ml/build_labels.py ml_features.bin

# 3. Train regime-specific models
python3 ml/train_expectancy_models.py train training_data.parquet models XAUUSD

# 4. Models exported to models/*.onnx
```

---

## SPEED-OPTIMISED EXECUTION

### Core Philosophy (LOCKED)
1. We do not trade more because we are fast
2. We trade tighter because we are fast
3. We accept flat days
4. We never override the -$200 stop
5. We let rare days run

### Speed Files
| File | Purpose |
|------|---------|
| `include/speed/SpeedOptimizedThresholds.hpp` | Per-instrument thresholds |
| `include/speed/SpeedEdgeMetrics.hpp` | 5 live dashboard indicators |

### Speed Dashboard Indicators
1. **Latency Edge**: peer_latency - our_latency (GREEN ≥1.5ms)
2. **Scratch Saved**: trades scratched that would have hit stop
3. **Early Entry**: our_entry - market_median (GREEN <-80ms)
4. **Burst Capture**: captured/total move (GREEN >35%)
5. **Speed EV**: EV_fast - EV_slow (must be positive)

### Instrument Thresholds
| Symbol  | Tier | Lat Block | Size vs NAS | Session |
|---------|------|-----------|-------------|---------|
| NAS100  | 1    | 5.0 ms    | 100%        | All     |
| US30    | 2    | 6.0 ms    | 70%         | NY only |
| SPX500  | 2    | 5.0 ms    | 60%         | NY only |
| XAUUSD  | 3    | 4.0 ms    | 50%         | All     |
| BTC/ETH | 4    | 2.5 ms    | 15-20%      | All     |

---

## EXECUTION GUARD ORDER (FIXED)

```cpp
// submitOrder() guard sequence
1. GlobalRiskGovernor::canSubmitOrder() // -$200 daily stop
2. NAS100 ownership
3. Index CFD session (US30, SPX500)
4. Engine ownership allowlist  // ← MOVED before latency
5. Latency gate
6. Size scaling
7. Submit
```

---

## DEPLOYMENT

```bash
cd ~
rm -rf chimera_src
unzip -o /mnt/c/Chimera/chimera_v4_6_0.zip
cd chimera_src
mkdir build && cd build
cmake ..
make -j4
./chimera
```

---

## OUTPUT FILES

After running, you'll have:
- `ml_features.bin` - Binary feature snapshots (every 100th tick)
- `ml_attribution.bin` - Per-trade ML metrics
- `logs/chimera_*.log` - Full console log

Export to CSV:
```bash
python3 ml/export_features.py ml_features.bin features.csv
```

---

## VERSION HISTORY

- v4.6.0: ML Execution System + Speed-optimised thresholds
- v4.5.1: Daily loss guard -$200, engine ownership enforcement
- v4.5.0: Triple engine architecture, NAS100 time-based ownership
- v4.4.0: Income engine, stand-down circuit breaker
