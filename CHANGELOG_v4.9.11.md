# Chimera v4.9.11 - Institutional Bootstrap & Execution System

## Release Date: 2025-01-03

## Overview
This release implements a complete institutional-grade execution system:
1. **Bootstrap Probe System** - Measures real ACK latency before trading (fixes chicken-egg deadlock)
2. **Execution Mode Selector** - Dynamic maker/taker switching based on conditions
3. **Session Weights** - Time-of-day edge and size adjustments
4. **Volatility-Aware Sizing** - Press when it pays, shrink when it doesn't
5. **Threshold Adapter** - Auto-relax when starving, auto-tighten on drawdown
6. **Execution Quality Feedback** - Closes the loop from fills to thresholds
7. **Hardened Shutdown** - Crash-proof signal handling

## Architecture Summary

### The Bootstrap Flow
```
STARTUP
   ↓
BOOTSTRAP MODE (system_mode = BOOTSTRAP)
   ↓
Send probe orders (10% below market, immediately cancelled)
   ↓
Measure ACK latency (nanosecond precision)
   ↓
After N probes per symbol → LIVE MODE
   ↓
Normal trading with latency-aware gates
```

### Institutional Execution Stack
```
Signal Generation
   ↓
Market State Classification (TRENDING/RANGING/VOLATILE/DEAD)
   ↓
Session Weight Adjustment (ASIA/LONDON/NY/OVERLAP)
   ↓
Execution Mode Selection (MAKER/TAKER/ADAPTIVE)
   ↓
Size Scaling (volatility + win_rate + latency + drawdown)
   ↓
Threshold Adaptation (auto-relax/tighten)
   ↓
Order Execution
   ↓
Quality Feedback Loop → adjust thresholds/mode
```

## New Files

### Bootstrap System
- `include/runtime/SystemMode.hpp` - BOOTSTRAP vs LIVE mode
- `include/bootstrap/LatencyBootstrapper.hpp` - Probe engine

### Institutional Execution Layer
- `include/execution/ExecutionModeSelector.hpp` - Dynamic maker/taker
- `include/execution/SessionWeights.hpp` - Time-of-day adjustments
- `include/execution/SizeScaler.hpp` - Volatility-aware sizing
- `include/execution/ThresholdAdapter.hpp` - Auto-relax/tighten
- `include/execution/ExecutionQualityFeedback.hpp` - Feedback loop
- `include/execution/SignalHandler.hpp` - Hardened shutdown

## Modified Files

### Core Changes
- `include/latency/LatencyGate.hpp` - BOOTSTRAP bypass
- `crypto_engine/include/binance/BinanceOrderSender.hpp` - Probe orders
- `crypto_engine/include/binance/BinanceEngine.hpp` - Probe integration
- `crypto_engine/include/microscalp/CryptoMicroScalp.hpp` - currentMid()
- `include/gui/GUIBroadcaster.hpp` - System mode display
- `chimera_dashboard.html` - Probe progress UI
- `src/main_triple.cpp` - Bootstrap wiring

## Key Features

### 1. Bootstrap Probe System
- Sends limit orders 10% below market
- Records ACK latency (nanoseconds)
- Cancels immediately after ACK
- Per-symbol warmup configuration

| Symbol   | Target Probes | Min Confidence | Price Offset |
|----------|---------------|----------------|--------------|
| BTCUSDT  | 30            | 25             | 10%          |
| ETHUSDT  | 25            | 20             | 8%           |
| SOLUSDT  | 20            | 15             | 7%           |

### 2. Execution Mode Selector
Decision logic:
- Crypto → TAKER (no queue position without colo)
- High volatility → TAKER (urgency)
- High reject rate → TAKER (venue instability)
- Low fill rate → TAKER (queue not viable)
- Otherwise → MAKER_FIRST

### 3. Session Weights
| Session     | Edge Weight | Size Mult |
|-------------|-------------|-----------|
| OVERLAP     | 1.30        | 1.20      |
| LONDON      | 1.10        | 1.00      |
| NEW_YORK    | 1.20        | 1.00      |
| ASIA        | 0.70        | 0.80      |
| OFF_HOURS   | 0.50        | 0.50      |

### 4. Size Scaling Factors
- Volatility boost: +20% when vol > 1.2x baseline
- Win rate boost: +10% when win rate > 55%
- Latency penalty: -30% when unstable
- Drawdown: -60% at severe (>4% DD)
- Clamp: [0.25x, 2.0x]

### 5. Threshold Adaptation
- Starvation relax: -10% after 15 min no trades
- Drawdown tighten: +25% at 3% DD
- Reject tighten: +15% when rejects > 10%
- Win streak relax: -5% after 3 wins
- Loss streak tighten: +10% after 2 losses

### 6. Hardened Shutdown
- SIGINT/SIGTERM handling
- Cancel all open orders
- Flatten positions (optional)
- Persist profiles
- Clean exit

## Audit Fixes Addressed

✅ **Bootstrap Deadlock** - Probe orders solve chicken-egg
✅ **RANGING → NO_TRADE** - Already correct in MarketState.hpp (MEAN_REVERSION)
✅ **Maker on Crypto** - ExecutionModeSelector forces TAKER
✅ **Latency as Cost** - Bootstrap + adaptive thresholds
✅ **GUI Truth** - Shows real ACK latency and probe progress
✅ **Shutdown Safety** - Signal handlers with callbacks

## Key Guarantees

| Feature | Status |
|---------|--------|
| No deadlock | ✅ Bootstrap bypass |
| Real latency | ✅ Probe ACK measurement |
| No PnL risk | ✅ Probes far from market |
| Per-symbol | ✅ Independent bootstrap |
| Auto-adapt | ✅ Thresholds + mode selection |
| Crash-safe | ✅ Signal handlers |

## What This Enables

✔ Trade ranging markets safely (mean reversion)
✔ Adapt to broker latency differences
✔ Switch maker ↔ taker automatically
✔ Avoid reject storms
✔ Scale when volatility pays
✔ Back off when venue degrades
✔ Persist learning across restarts
✔ Tell you the truth in the GUI

## What It Does NOT Do

❌ Win crypto maker queues without colo
❌ Beat HFTs on microsecond arb
❌ Magically fix bad brokers
❌ Generate alpha (it executes alpha, doesn't create it)

## Migration Notes

- No config changes required
- System auto-starts in BOOTSTRAP mode
- First ~30 seconds shows probe activity
- After bootstrap, normal trading with real latency data
- Institutional layers are opt-in (include headers to use)

## Testing

1. Start Chimera
2. Watch dashboard for `PROBE X/Y` progress
3. Verify transition to `LIVE` after ~30s
4. Check HOT LAT panel shows real p50/p99 values
5. Confirm trades flow after bootstrap complete
6. Test Ctrl+C shutdown (should be clean)
