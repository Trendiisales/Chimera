# Chimera v4.4.2 - Core Fixes

## What Was Fixed

### 1. Depth Stream (CRITICAL)
**Problem:** Using `depth5@100ms` (snapshot-only, no diffs)  
**Fix:** Changed to `depth@100ms` (continuous diff stream)  
**Impact:** Regime detection now works, book stays fresh

**File:** `src/BinanceDepthWS.cpp` line 74

### 2. Shutdown Speed
**Problem:** `lws_service()` blocking 50ms  
**Fix:** Changed to 10ms timeout  
**Impact:** Shutdown < 50ms (was hanging)

**File:** `src/BinanceDepthWS.cpp` line 96

### 3. Exit Logging
**Added:** Clean exit messages after context destroy  
**Impact:** Confirms proper thread termination

**File:** `src/BinanceDepthWS.cpp` line 116

---

## Expected Console Output

**On Startup:**
```
[DEPTH_WS] ✅ Connected to /ws/ethusdt@depth@100ms
[DEPTH_WS] ✅ Connected to /ws/solusdt@depth@100ms
```

**On Ctrl+C:**
```
[CHIMERA] Tick loop stopped
[SHUTDOWN] Initiating graceful shutdown...
[DEPTH_WS] Clean exit: ethusdt
[DEPTH_WS] Clean exit: solusdt
[SHUTDOWN] ✅ Clean exit
```

**Timing:** < 50ms from SIGINT to exit

---

## Deploy

```bash
scp -i ~/.ssh/ChimeraKey.pem ~/Downloads/Chimera_v4.4.2.tar.gz ubuntu@56.155.82.45:~/
ssh -i ~/.ssh/ChimeraKey.pem ubuntu@56.155.82.45
mv ~/Chimera ~/Chimera_old_$(date +%Y%m%d_%H%M%S)
tar xzf Chimera_v4.4.2.tar.gz
cd ~/Chimera/build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j2
cd ~/Chimera && ./build/chimera_real
```

---

## Verification

**Good:**
- Sees `@depth@100ms` in startup logs (NOT `depth5`)
- Shutdown completes in < 1 second
- Regime not stuck in INVALID
- Both ETH and SOL depth counters incrementing

**Bad (report immediately):**
- Still sees `depth5` 
- Shutdown hangs
- Regime always INVALID
- SOL depth counter frozen at 0

---

## What's Still Intact

- EdgeLeakTracker (all v4.4.0 features)
- Cost floor timing fix
- DepthLag threshold fix (2000ms)
- SOL microstructure tuning
- Snapshot throttle (all v4.4.1 fixes)

---

**Checksum:** `e8f6bf87ccd7291cbc002b884fabf4a9`  
**Size:** 53KB  
**Date:** 2026-01-27 07:33 UTC
