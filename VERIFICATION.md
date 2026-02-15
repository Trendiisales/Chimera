# Chimera Production - System Verification

## Critical Verification Checklist

Before deploying to production, verify EVERY item below.

---

## ✅ Phase 1: File Integrity (MANDATORY)

### Core Trading Logic
- [ ] `engines/StructuralMomentumEngine.hpp` exists and unchanged
- [ ] `engines/CompressionBreakEngine.hpp` exists and unchanged
- [ ] `engines/StopCascadeEngine.hpp` exists and unchanged
- [ ] `engines/MicroImpulseEngine.hpp` exists and unchanged
- [ ] All 22 engine files from `ENGINES_MANIFEST.md` are present
- [ ] `core/V2Runtime.hpp` exists (manages engine execution)
- [ ] `core/V2Desk.hpp` exists (coordinates engines)

### Risk Management
- [ ] `risk/CapitalGovernor.hpp` exists
- [ ] `config/V2Config.hpp` exists with correct limits
- [ ] `execution/ExecutionAuthority.hpp` exists
- [ ] `supervision/Supervisor.hpp` exists

### FIX Integration
- [ ] `main.cpp` contains `FixSession` class
- [ ] `fix_build()` function implements FIX 4.4 protocol
- [ ] SSL/TLS support compiled (OpenSSL linked)

---

## ✅ Phase 2: Build Verification (MANDATORY)

```bash
cd chimera_production
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Build Checks
- [ ] CMake completes without errors
- [ ] Compilation completes without errors
- [ ] Binary `chimera` is created in `build/`
- [ ] Binary size > 1MB (indicates full system)
- [ ] No "undefined reference" linker errors

### Dependency Check
```bash
ldd chimera
```
- [ ] `libssl.so` is linked
- [ ] `libcrypto.so` is linked
- [ ] `libpthread.so` is linked
- [ ] No missing dependencies

---

## ✅ Phase 3: Configuration Verification (MANDATORY)

### Environment Variables
```bash
echo $FIX_USERNAME
echo $FIX_PASSWORD
```
- [ ] `FIX_USERNAME` is set to: `live.blackbull.8077780`
- [ ] `FIX_PASSWORD` is set to: `8077780`

### Shadow Mode (CRITICAL)
Open `main.cpp` and verify line ~465:
```cpp
shadow_gate.set_shadow(true);  // MUST BE TRUE
```
- [ ] Shadow mode is ENABLED (`true`)
- [ ] If `false`, **DO NOT PROCEED** (live orders will be sent)

### Risk Limits
Check `config/V2Config.hpp`:
```cpp
constexpr double DAILY_MAX_LOSS = 200.0;  // NZD
constexpr double LOT_SIZE = 0.01;
constexpr int MAX_CONCURRENT_TOTAL = 4;
```
- [ ] `DAILY_MAX_LOSS` is reasonable (suggested: 200 NZD)
- [ ] `LOT_SIZE` is micro lots (0.01)
- [ ] `MAX_CONCURRENT_TOTAL` limits exposure

---

## ✅ Phase 4: Runtime Verification (MANDATORY)

### Test 1: Basic Startup
```bash
cd build
./chimera
```

Expected output:
```
============================================================
  CHIMERA PRODUCTION - Unified Trading System
============================================================
  Engines:    4 Active (Structural, Compression, Cascade, Micro)
  FIX:        live-uk-eqx-01.p.c-trader.com
...
[V2DESK] Initialized with 4 engines:
  - StructuralMomentumEngine
  - CompressionBreakEngine
  - StopCascadeEngine
  - MicroImpulseEngine

[FIX] Connected to live-uk-eqx-01.p.c-trader.com:5211
[FIX] Sent Logon (seq=1)
```

**Verification:**
- [ ] System starts without segfault
- [ ] All 4 engines are listed
- [ ] FIX connection succeeds
- [ ] No immediate errors

### Test 2: Telemetry Endpoint
In another terminal:
```bash
curl http://localhost:8080
```

Expected JSON response:
```json
{
  "timestamp": 1708012345678,
  "shadow_mode": true,
  "fix_connected": true,
  "portfolio": { ... },
  "xauusd": { ... },
  "xagusd": { ... }
}
```

**Verification:**
- [ ] Telemetry responds on port 8080
- [ ] `"shadow_mode": true` is present
- [ ] `"fix_connected": true` after logon
- [ ] All symbols have data

### Test 3: Engine Activity (5 minutes)
Let system run for 5 minutes, then check console:

```
[STATUS] PnL: 0.00 | Float: 0.00 | Open: 0 | XAU RTT: 5.2ms | XAG RTT: 6.1ms
```

**Verification:**
- [ ] RTT measurements appear (< 25ms acceptable)
- [ ] PnL updates (even if zero)
- [ ] No crashes or freezes
- [ ] Memory usage stable (`top` or `htop`)

---

## ✅ Phase 5: Engine Validation (48 HOURS SHADOW)

### Proposal Generation Test
Monitor console logs for:
```
[ENGINE] StructuralMomentumEngine: BUY proposal XAUUSD @ 2650.50
[ENGINE] CompressionBreakEngine: SELL proposal XAGUSD @ 30.25
```

**Verification:**
- [ ] Engines generate proposals (proves they're running)
- [ ] Proposals are logged but **NOT executed** (shadow mode)
- [ ] Multiple engines fire over time
- [ ] No duplicate proposal spam

### PnL Simulation Test
After 1 hour of shadow mode:
```bash
curl http://localhost:8080 | jq '.portfolio.daily_pnl'
```

**Verification:**
- [ ] PnL changes over time (indicates engine activity)
- [ ] PnL stays within expected range (-50 to +50 NZD/hr reasonable)
- [ ] Daily loss limit has NOT triggered prematurely
- [ ] Floating PnL updates continuously

### Latency Test
Monitor RTT over 24 hours:
```bash
watch -n 1 'curl -s http://localhost:8080 | jq ".xauusd.rtt_ms, .xagusd.rtt_ms"'
```

**Verification:**
- [ ] Average RTT < 10ms
- [ ] Max RTT < 25ms
- [ ] No RTT spikes > 50ms
- [ ] Stable latency distribution

---

## ✅ Phase 6: Safety Mechanism Validation

### Test 1: Daily Loss Limit
Temporarily edit `config/V2Config.hpp`:
```cpp
constexpr double DAILY_MAX_LOSS = 1.0;  // Test with $1 limit
```
Rebuild and run. System should shut down quickly.

**Verification:**
- [ ] System detects loss limit breach
- [ ] Auto-shutdown occurs
- [ ] Log message: "Daily loss limit reached"
- [ ] No orders sent after limit hit

**IMPORTANT:** Restore real limit after test!

### Test 2: Connection Loss
Disconnect network while system running:
```bash
sudo ifconfig eth0 down
sleep 30
sudo ifconfig eth0 up
```

**Verification:**
- [ ] System detects FIX disconnection
- [ ] `fix_connected: false` in telemetry
- [ ] Execution governor blocks trading
- [ ] System reconnects when network restored

### Test 3: Shadow Gate
Verify in code that `allow_live()` is checked before order transmission.

Search `main.cpp` for:
```cpp
if (!shadow_gate.allow_live()) {
    // Order blocked
    return false;
}
```

**Verification:**
- [ ] Shadow gate check exists
- [ ] Orders are blocked when shadow mode enabled
- [ ] No `NewOrderSingle` FIX messages sent (verify with Wireshark if paranoid)

---

## ✅ Phase 7: Performance Validation

### CPU Affinity Test
```bash
ps -eLo pid,tid,psr,comm | grep chimera
```

Expected output shows threads on cores 0, 1, 2, 3:
```
12345  12346   0  chimera     # FIX reader
12345  12347   1  chimera     # XAU engine
12345  12348   2  chimera     # XAG engine
12345  12349   3  chimera     # Telemetry
```

**Verification:**
- [ ] 4 threads are running
- [ ] Threads are pinned to different cores
- [ ] No excessive thread creation/destruction

### Memory Test
```bash
valgrind --leak-check=full ./chimera
```
(Warning: This will slow system significantly)

**Verification:**
- [ ] No memory leaks detected
- [ ] All allocations are freed on shutdown
- [ ] Heap usage is stable over time

### Stress Test (Optional)
Simulate high-frequency market data:
- Modify FIX simulator to send 1000 ticks/sec
- Run for 10 minutes
- Monitor CPU and memory

**Verification:**
- [ ] System handles load without lag
- [ ] Telemetry remains responsive
- [ ] No buffer overflows or crashes

---

## ✅ Phase 8: Pre-Live Checklist (DO NOT SKIP)

Before enabling live trading (`shadow_gate.set_shadow(false)`):

### 48-Hour Shadow Run
- [ ] System ran for 48+ hours without crash
- [ ] No memory leaks observed
- [ ] All engines generated proposals
- [ ] PnL calculations are accurate
- [ ] Latency stayed under 10ms average
- [ ] Daily loss limit works correctly
- [ ] Connection recovery works
- [ ] Telemetry is stable

### Risk Review
- [ ] Daily loss limit is acceptable (200 NZD recommended)
- [ ] Lot size is micro (0.01)
- [ ] Max concurrent positions is conservative (4 or less)
- [ ] Stop-loss logic is verified
- [ ] Position sizing is correct

### Monitoring Setup
- [ ] Telemetry dashboard is running
- [ ] Alerts are configured (PagerDuty, SMS, etc.)
- [ ] Logs are being saved to disk
- [ ] Backup system is ready if primary fails

### Manual Override
- [ ] You know how to stop the system (`Ctrl+C` or `kill -TERM`)
- [ ] You know how to disable specific engines
- [ ] You know how to manually close positions
- [ ] Emergency contact list is ready

---

## ✅ Phase 9: Live Transition (EXTREME CAUTION)

### Enabling Live Orders
1. Stop the system
2. Edit `main.cpp` line ~465:
   ```cpp
   shadow_gate.set_shadow(false);  // ENABLE LIVE TRADING
   ```
3. Rebuild: `cd build && make`
4. **Double-check** all previous checklist items
5. Start system: `./chimera`
6. Monitor CONSTANTLY for first 30 minutes

### First 30 Minutes Live
- [ ] Watch every order transmission (console logs)
- [ ] Verify fills match expectations
- [ ] Check actual PnL vs. shadow PnL
- [ ] Monitor for erratic behavior
- [ ] Have kill switch ready

### First 24 Hours Live
- [ ] Check every 2 hours
- [ ] Compare live PnL to shadow projections
- [ ] Look for any anomalies
- [ ] Verify risk limits are respected
- [ ] Monitor broker account balance

---

## ✅ Known Risks

### What Could Go Wrong
1. **Order flood** - Too many orders submitted
   - Mitigation: `MAX_CONCURRENT_TOTAL = 4` limit
2. **Connection loss during position** - Market moves against you
   - Mitigation: Daily loss limit auto-shutdown
3. **Latency spike** - Missed entries/exits
   - Mitigation: Execution governor blocks > 25ms RTT
4. **Engine bug** - Faulty logic generates bad trades
   - Mitigation: 48-hour shadow testing, manual review
5. **Capital depletion** - Slow bleed of account
   - Mitigation: Daily max loss, regular PnL review

### Emergency Procedures
**If system goes haywire:**
1. `Ctrl+C` to stop (graceful shutdown)
2. If frozen: `kill -9 <PID>` (force kill)
3. Log into broker and **manually close all positions**
4. Review logs: `tail -n 1000 chimera.log`
5. Contact developer/support

---

## ✅ Sign-Off

I have verified ALL items in this checklist:

**Name:** _______________________  
**Date:** _______________________  
**Signature:** _______________________

**Shadow Mode Duration:** _______ hours  
**Average RTT:** _______ ms  
**Observed Max Loss:** NZD _______  
**Ready for Live:** YES / NO

---

## Final Warning

**Trading is risky. This system can lose money quickly.**

Even with all checks passed:
- Start with minimal capital (e.g., $100)
- Monitor continuously for first week
- Be prepared to shut down instantly
- Accept that losses WILL occur

**No guarantee of profit. Use at your own risk.**

---

Last Updated: 2025-02-15  
Version: 1.0.0
