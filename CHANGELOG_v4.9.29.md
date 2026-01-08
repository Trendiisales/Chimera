# Chimera v4.9.29 - COMPLETE PROBE FIX

## Release Date: 2025-01-04

## Critical Bug Fixes

### Problem
GUI showed `HOT LAT 0.00 ms | NO_DATA` - no latency measurements despite probe system being "implemented".

### Root Causes (TWO gates blocking probes)

**Gate 1: `is_running()` blocking tick feeding**
```cpp
// BROKEN - probe ticks inside is_running() block
if (binance_engine.is_running()) {
    probe_ctrl.onTick(...);  // NEVER CALLED when INIT [NO_TRADE]
}
```

**Gate 2: Time sync blocking probe SENDING**
```cpp
// In BinanceOrderSender::send_probe_order()
if (!BinanceTimeSync::is_initialized()) {
    return false;  // BLOCKS ALL PROBES
}
```

### The Complete Fix

**Fix 1: Separate probe tick block using `isConnected()`**
```cpp
// v4.9.29: Probes only need WS connection, not RUNNING state
if (ENABLE_PROBES && binance_engine.isConnected()) {
    // Check time sync first
    if (!BinanceTimeSync::is_initialized()) {
        // Retry time sync every 10 seconds
        binance_engine.syncServerTime(server_time_ms);
        goto skip_probe_ticks;
    }
    
    probe_ctrl.onTick("BTCUSDT", mid);  // NOW FIRES
}
```

**Fix 2: Time sync retry mechanism**
- If initial time sync failed at startup, probes were permanently blocked
- Now retries time sync every 10 seconds in main loop
- Logs clear status: `[PROBE_CTRL] ⚠️ TIME SYNC NOT COMPLETE`

**Fix 3: Added `syncServerTime()` to BinanceEngine**
```cpp
// New public method for time sync recovery
bool syncServerTime(uint64_t& server_time_ms) noexcept;
```

## Changes

### main_dual.cpp
- Added time sync check at start of probe block
- Added 10-second retry loop for failed time sync
- Added `skip_probe_ticks` label for clean flow control
- Added diagnostic logging for time sync status

### BinanceEngine.hpp
- Added `syncServerTime()` public method for retry capability

## Expected Behavior After Fix

### Startup (time sync OK)
```
[BinanceEngine] ✓ Time sync complete (offset: +15 ms)
[PROBE_CTRL] ✓ Enabled (WS connected + time synced)
[PROBE_CTRL] BTCUSDT sending IOC probe @ 95000.00 qty=0.000100
[PROBE-ACK] client_id=2000000 exchange_id=123456789
[PROBE_CTRL] ✓ BTCUSDT ACK latency=2.341ms (sample 1/5)
```

### Startup (time sync failed, then recovered)
```
[BinanceEngine] ⚠️ CRITICAL: Could not sync Binance server time after 3 retries!
[PROBE_CTRL] ⚠️ TIME SYNC NOT COMPLETE - probes blocked until sync succeeds
[PROBE_CTRL] Will retry time sync every 10 seconds...
[PROBE_CTRL] Retrying time sync...
[PROBE_CTRL] ✓ Time sync recovered! Probes can now fire.
[PROBE_CTRL] ✓ Enabled (WS connected + time synced)
```

### GUI
- HOT LAT: `2.34ms` (instead of `NO_DATA`)
- PROBE: `1/42 (1 sample)` (instead of `0/42 (0 samples)`)

## Deploy Command

```bash
rm -rf ~/Chimera ~/Chimera_v4_9_29
cp /mnt/c/Chimera/Chimera_v4_9_29_COMPLETE_PROBE_FIX.zip ~/
cd ~ && unzip -o Chimera_v4_9_29_COMPLETE_PROBE_FIX.zip && mv Chimera_v4_9_29 Chimera
cd ~/Chimera && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
./chimera
```

## Diagnostic Checklist

After deployment, check logs for:

| Log Message | Meaning |
|-------------|---------|
| `[BinanceEngine] ✓ Time sync complete` | Time sync OK at startup |
| `[PROBE_CTRL] ✓ Enabled` | Probes activated |
| `[PROBE_CTRL] BTCUSDT sending IOC probe` | Probe being sent |
| `[PROBE-ACK]` | Binance acknowledged probe |
| `[PROBE_CTRL] ⚠️ TIME SYNC NOT COMPLETE` | Time sync failed - will retry |
| `[PROBE_CTRL] ✓ Time sync recovered!` | Retry succeeded |
| `[PROBE_BLOCKED] Time sync not complete` | Probes blocked (from OrderSender) |

## Files Modified

```
src/main_dual.cpp                              - Probe tick block + time sync retry
crypto_engine/include/binance/BinanceEngine.hpp - Added syncServerTime() method
```
