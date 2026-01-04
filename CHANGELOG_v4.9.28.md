# Chimera v4.9.28 - PROBE TICK GATE FIX

## Release Date: 2025-01-04

## Critical Bug Fix

### Problem
GUI showed `HOT LAT 0.00 ms | NO_DATA` - no latency measurements appearing despite probe system being "implemented" in v4.9.27.

### Root Cause
The **entire crypto tick broadcast block** was gated by `is_running()`:

```cpp
// BROKEN - line 717 in v4.9.27
if (binance_engine.is_running() && 
    (now_ms - last_crypto_broadcast_ms) >= CRYPTO_BROADCAST_INTERVAL_MS) {
    // ... probe_ctrl.onTick() inside here ...
}
```

When the engine is in `INIT [NO_TRADE]` state:
- `is_running()` returns **false**
- The entire block is skipped
- `probe_ctrl.onTick()` is **never called**
- Probes **never fire**
- **NO_DATA forever**

The probe enable check (line 728) correctly used `isConnected()`, but it was useless because the tick feeding itself was blocked by the outer `is_running()` gate.

### The Fix

Created a **separate probe tick feeding block** that uses `isConnected()` instead of `is_running()`:

```cpp
// v4.9.28: PROBE TICK FEEDING - SEPARATE from trading state!
// Probes are TELEMETRY, not trading - only need WebSocket connection
if (ENABLE_PROBES && binance_engine.isConnected() && 
    (now_ms - last_probe_tick_ms) >= PROBE_TICK_INTERVAL_MS) {
    
    // Enable probes when connected (not when running!)
    // Feed ticks to ProbeController
}

// GUI tick broadcast - still requires is_running()
if (binance_engine.is_running() && ...) {
    // GUI updates only, no probe_ctrl.onTick()
}
```

### Key Architectural Insight

**Probes are TELEMETRY, not TRADING.**

| Concern | Requires |
|---------|----------|
| Trading | `is_running()` (engine in RUNNING state) |
| Probes | `isConnected()` (WebSocket connected) |
| GUI Ticks | `is_running()` (engine in RUNNING state) |

The engine can be in `INIT [NO_TRADE]` state for various reasons (bootstrap, filtering, regime), but probes should still measure latency as long as the WebSocket is connected.

## Changes

### main_dual.cpp

1. **Added separate probe tick interval tracker**
   ```cpp
   uint64_t last_probe_tick_ms = 0;
   constexpr uint64_t PROBE_TICK_INTERVAL_MS = 100;
   ```

2. **Created new probe tick feeding block** (uses `isConnected()`)
   - Handles probe enable logic
   - Feeds BTCUSDT, ETHUSDT, SOLUSDT ticks to ProbeController
   - Runs when WebSocket connected, regardless of engine state

3. **Removed probe code from is_running() block**
   - GUI tick broadcasts no longer call `probe_ctrl.onTick()`
   - Added comments explaining the separation

4. **Updated version to v4.9.28**

## Expected Behavior After Fix

### Log Output
```
[PROBE_CTRL] Registered BTCUSDT (fixed qty=0.000100)
[PROBE_CTRL] ✓ Enabled (WS connected, engine state irrelevant for probes)
[PROBE_CTRL] BTCUSDT sending IOC probe @ 95000.00 qty=0.000100
[PROBE_CTRL] BTCUSDT SENT probe id=2000000 [WAITING FOR ACK]
[PROBE-ACK] client_id=2000000 exchange_id=123456789
[PROBE_CTRL] ✓ BTCUSDT ACK latency=2.341ms (sample 1/5)
```

### GUI
- HOT LAT: `2.34ms` (instead of `NO_DATA`)
- PROBE: `1/42 (1 sample)` (instead of `0/42 (0 samples)`)

## Deploy Command

```bash
rm -rf ~/Chimera ~/Chimera_v4_9_28
cp /mnt/c/Chimera/Chimera_v4_9_28_PROBE_TICK_GATE_FIX.zip ~/
cd ~ && unzip -o Chimera_v4_9_28_PROBE_TICK_GATE_FIX.zip && mv Chimera_v4_9_28 Chimera
cd ~/Chimera && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
./chimera
```

## Files Modified

```
src/main_dual.cpp  - Separated probe tick feeding from GUI tick broadcasting
```

## Verification Checklist

After deployment, confirm:
1. `[PROBE_CTRL] ✓ Enabled` appears in logs
2. `[PROBE_CTRL] BTCUSDT sending IOC probe` appears within ~2 seconds
3. `[PROBE-ACK]` or `[PROBE-REJECT]` appears
4. HOT LAT shows a value (not `NO_DATA`)
5. PROBE counter increments

If `NO_DATA` persists, check:
- WebSocket connection status (should show "Connected")
- Time sync completion (should not see `[PROBE_BLOCKED] Time sync not complete`)
- Any signature errors in logs
