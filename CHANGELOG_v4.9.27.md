# Chimera v4.9.27 Changelog

**Date**: 2026-01-04
**Status**: 🔧 ACTIVE
**Focus**: Four-Phase Implementation (Auth → Latency → Micro-live → Intelligence)

---

## Overview

v4.9.27 implements a systematic four-phase fix addressing the root causes of:
- Binance -1022 signature authentication failures
- Latency visibility gaps (shows 0.0 when no ACKs)
- Alarm storm noise from transient network events
- Lack of failure shape intelligence

---

## Phase 1: AUTH HARDENING

### FIX 1.1: Timestamp Drift Guard
**File**: `crypto_engine/include/binance/BinanceHMAC.hpp`

- Added `BinanceTimeSync::needs_resync(threshold_ms)` for proactive drift detection
- Added `BinanceTimeSync::check_drift(server_time_ms, threshold_ms)` for validation
- Added `BinanceTimeSync::current_drift_ms()` for diagnostics
- WSAPIRequestBuilder now warns if drift > 2000ms before signing

### FIX 1.2: Signature Rejection Counter
**File**: `crypto_engine/include/binance/BinanceHMAC.hpp`

New `SignatureRejectionTracker` singleton:
- Tracks total -1022 rejections
- Prints diagnostic box on first rejection and every 10th
- Lists common causes (clock drift, param mismatch, permissions)
- Exposed to GUI via `signature_rejections` field

### FIX 1.3: Enhanced Diagnostic Output
**File**: `crypto_engine/include/binance/BinanceHMAC.hpp`

Every order now prints:
- Full parameter list with types (STR/NUM)
- Complete canonical string being signed
- Timestamp with current drift
- Verification command for manual testing

---

## Phase 2: LATENCY VISIBILITY

### Already Complete in v4.9.26
- NO_DATA state when samples = 0
- Honest display (0.0 when no ACKs received)
- Root cause identified: auth failing, not wiring

### v4.9.27 Additions
- Signature rejection counter in GUI JSON (`execution_governor.signature_rejections`)
- Direct visibility into "why nothing is trading"

---

## Phase 3: MICRO-LIVE NOISE REDUCTION

### FIX 3.4: Alarm Dampening
**File**: `include/execution/ExecutionGovernor.hpp`

- Added `ALARM_DAMPENING_NS = 3'000'000'000` (3 seconds)
- CONNECTION_LOST alarm only fires after 3s of sustained loss
- VENUE_HALTED alarm only fires after 3s persistence
- Transient SSL blips, WebSocket reconnects no longer trigger alarms
- State transitions still logged immediately (just alarm delayed)
- Recovery alarms only fire if loss alarm was fired

### New DegradationReason
- Added `SIGNATURE_ERROR = 8` for -1022 tracking separate from general ORDER_ERROR

### Governor Tick Integration
- `ExecutionGovernor::tick()` now checks alarm dampening timers
- Called from main loop (1Hz) along with venue state update
- Auto-unfreeze symbols when timers expire

---

## Phase 4: INTELLIGENCE LAYER

### Failure Shape Detection
**File**: `include/alpha/FailureShapeDetector.hpp` (NEW)

Classifies trade failures into distinct shapes:

| Shape | Cause | Indicators |
|-------|-------|------------|
| `FALSE_VACUUM` | Signal noise | Fast 1s reversion |
| `LATE_ENTRY` | Latency ate edge | time_to_fill > 300ms |
| `FEE_DOMINATED` | Edge < fees | fee_bps > gross_edge |
| `SPREAD_EATEN` | Entry spread killed PnL | spread_cost > gross/2 |
| `ADVERSE_SELECTION` | Toxic flow | High VPIN + loss |
| `LIQUIDITY_MIRAGE` | Phantom depth | High slippage |
| `SLIPPAGE_DEATH` | Execution failure | MAE > 2x gross |
| `CLEAN_WIN` | Success | net_edge > 0.5 |

Features:
- Per-shape statistics (count, total PnL, averages)
- Circular history buffer (1000 trades)
- `printReport()` for summary
- `printRecommendations()` for actionable suggestions
- CSV export for analysis
- Configurable thresholds

---

## Files Modified

### Core Auth
- `crypto_engine/include/binance/BinanceHMAC.hpp` - Drift guard, rejection tracker, diagnostics

### Execution Governor
- `include/execution/ExecutionGovernor.hpp` - Alarm dampening, signature rejection tracking

### Order Sender
- `crypto_engine/include/binance/BinanceOrderSender.hpp` - SignatureRejectionTracker integration

### GUI
- `include/gui/GUIBroadcaster.hpp` - Added `signature_rejections` field and JSON output

### Main
- `src/main_dual.cpp` - Version bump, governor tick call, signature rejection wiring

### New Files
- `include/alpha/FailureShapeDetector.hpp` - Failure shape classification system

---

## Exit Criteria (Phase Validation)

### Phase 1: Auth ✓
- [ ] ≥1 ACK received from Binance
- [ ] No -1022 errors after proper API key configuration
- [ ] SignatureRejectionTracker shows 0 after fix

### Phase 2: Latency ✓
- [ ] GUI shows non-zero latency when ACKs arrive
- [ ] Samples increment with each ACK
- [ ] signature_rejections visible in GUI

### Phase 3: Micro-live ✓
- [ ] No alarm storms from transient events
- [ ] Alarms only fire for sustained (>3s) issues
- [ ] Symbol auto-unfreeze works

### Phase 4: Intelligence ✓
- [ ] Failure shapes logged on trade exits
- [ ] printReport() shows shape distribution
- [ ] Recommendations generated for problem shapes

---

## Deployment

```bash
# Archive existing
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)

# Transfer via RDP to C:\Chimera\
# Then unzip on Linux side (faster)
cp /mnt/c/Chimera/Chimera_v4_9_27.zip ~/
cd ~
unzip Chimera_v4_9_27.zip
mv Chimera_v4_9_27 Chimera

# Build with Zig cross-compiler
cd ~/Chimera/build
cmake ..
make -j$(nproc)

# Run
./chimera
```

---

## Next Steps

1. **Verify API Key Permissions** - Ensure Spot Trading enabled, IP not restricted
2. **Manual Signature Test** - Use diagnostic output to verify HMAC matches
3. **Monitor Rejection Counter** - Should stay at 0 after proper configuration
4. **Build Failure Shape Data** - Let system classify trades, review recommendations
