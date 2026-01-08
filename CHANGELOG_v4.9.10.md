# Chimera v4.9.10 - LATENCY-GATE: Honest Latency + Trade Gating

## 🎯 The Problem We Fixed

Your GUI latency was LYING to you. It was measuring:
- TCP connect time
- Reconnects
- Timeouts
- Wall-clock timestamps
- Averaged garbage

This made you think you had 0.2ms latency when reality was 1-5ms.

## ✅ What v4.9.10 Does

### 1. HONEST Hot-Path Latency Tracking

**NEW FILE: `include/latency/HotPathLatencyTracker.hpp`**
- Uses `CLOCK_MONOTONIC_RAW` (Linux) / `QueryPerformanceCounter` (Windows)
- Measures ONLY send → ACK latency (order submission to exchange response)
- Percentile-filtered (min, p10, p50, p90, p99) - NOT averages
- Filters out >5ms spikes (reconnects, GC, timeouts)

**What it measures:**
- Time from `ws_.send_text(order)` to receiving FILLED/NEW response
- This is the REAL hot-path latency your strategies depend on

**What it does NOT measure:**
- Network ping/RTT (use external tools)
- TCP connect time
- WebSocket handshake time
- Reconnect latency (filtered out)

### 2. Latency-Aware Trade Gating

**NEW FILE: `include/latency/LatencyGate.hpp`**

#### Core Rule: Block trades during degraded latency
```cpp
bool latency_ok =
    latency.p10_ms < 0.50 &&     // engine reacting fast
    latency.p50_ms < 3.0 &&      // system stable
    latency.min_ms > 0.0;        // sanity

if (!latency_ok) {
    block_reason = "LATENCY_DEGRADED";
    return NO_TRADE;
}
```

This kills trades during:
- CPU contention
- GC / page faults
- VPS noise / neighbor activity
- Network degradation

#### Edge Adjustment by Latency State
| Latency State | p10 Threshold | Required Edge |
|--------------|---------------|---------------|
| FAST         | < 0.30ms      | 6.0 bps       |
| NORMAL       | < 0.60ms      | 10.0 bps      |
| SLOW         | < 1.00ms      | 18.0 bps      |
| DEGRADED     | >= 1.00ms     | NO TRADE      |

This is how professionals use speed - latency NEVER replaces edge, it tightens/loosens required edge.

#### Maker vs Taker Routing
```cpp
if (latency.p10_ms > 0.40) {
    force_taker_only = true;  // Queue position lost before arrival
}
```

### 3. GUI Integration

New JSON fields in dashboard broadcast:
```json
{
  "hot_path_latency": {
    "min_ms": 0.14,
    "p10_ms": 0.31,
    "p50_ms": 2.7,
    "p90_ms": 4.2,
    "p99_ms": 4.8,
    "samples": 256,
    "spikes_filtered": 12,
    "state": "FAST",
    "exec_mode": "MAKER_FIRST"
  }
}
```

GUI now shows:
```
LATENCY (HOT PATH)
min:   0.14 ms
p10:   0.31 ms
p50:   2.7 ms

MODE:
ENGINE: FAST
STRATEGY: TAKER-ONLY
```

## Files Changed

### New Files
- `include/latency/HotPathLatencyTracker.hpp` - Hot-path latency measurement
- `include/latency/LatencyGate.hpp` - Trade gating based on latency

### Modified Files
- `crypto_engine/include/binance/BinanceOrderSender.hpp`
  - Added `HotPathLatencyTracker` integration
  - Records latency on FILL/NEW responses only
  - Exposes min/p10/p50/p90/p99 metrics
  
- `crypto_engine/include/binance/BinanceEngine.hpp`
  - Added `hotPathLatencySnapshot()` accessor
  - Added `hotPathLatency_*` metric getters
  
- `crypto_engine/include/microscalp/CryptoMicroScalp.hpp`
  - Added `LatencyGate` integration
  - Added `setLatencySnapshotCallback()` for latency data injection
  - Modified `checkEntryFilters()` to use LatencyGate
  - Modified `effectiveRouting()` to force TAKER_ONLY when latency high
  
- `include/gui/GUIBroadcaster.hpp`
  - Added hot-path latency fields to GUIState
  - Added `updateHotPathLatency()` method
  - Added JSON output for hot_path_latency
  
- `src/main_triple.cpp`
  - Wired latency snapshot callbacks to microscalp engines
  - Added hot-path latency GUI updates
  - Version bump to v4.9.10-LATENCY-GATE

## Configuration Presets

Three preset configurations available:

### HFT (Ultra-aggressive)
```cpp
latencyConfigHFT()
// p10 thresholds: 0.20 / 0.40 / 0.60 ms
// Edge requirements: 4 / 8 / 15 bps
// Maker viable: < 0.25ms
```

### Scalp (Default)
```cpp
latencyConfigScalp()
// p10 thresholds: 0.30 / 0.60 / 1.00 ms
// Edge requirements: 6 / 10 / 18 bps
// Maker viable: < 0.40ms
```

### Swing (Relaxed)
```cpp
latencyConfigSwing()
// p10 thresholds: 1.00 / 2.00 / 5.00 ms
// Edge requirements: 15 / 25 / 40 bps
// Maker viable: < 2.00ms
```

## Expected Impact

On your current VPS you will see:
- Min: ~0.10–0.30 ms
- P10: ~0.20–0.60 ms
- P50: ~1–5 ms

That is REAL, not fantasy.

Your network RTT is still "bad" — but now:
- Your engine metrics are truthful
- Your strategy logic can reason correctly
- Your GUI stops lying to you

## ✅ Final Checklist

- [x] Use CLOCK_MONOTONIC_RAW
- [x] Measure send → ACK only
- [x] Percentile filter (not average)
- [x] Ignore reconnect samples (>5ms)
- [x] Display min / p10 / p50
- [x] Rename metric honestly
- [x] Block trades when degraded
- [x] Adjust edge by latency
- [x] Force TAKER when maker not viable

## What Latency CAN Do

✅ Prevent bad trades
✅ Reduce overtrading
✅ Adapt aggressiveness
✅ Enforce honesty in strategy
✅ Improve expectancy

## What Latency CANNOT Do

❌ Replace edge analysis
❌ Guarantee fills
❌ Make network faster
❌ Fix bad microstructure calls
