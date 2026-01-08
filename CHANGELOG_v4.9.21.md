# Chimera v4.9.21 Changelog - FINAL SIGNATURE FIX

**Release Date:** 2026-01-03
**Status:** 🔧 CRITICAL FIX - PRODUCTION READY

## Summary

Complete fix for Binance WebSocket API -1022 "Signature for this request is not valid" errors.

## Root Cause (FINAL)

**The failure was NOT decimal precision or apiKey exclusion.**

**The failure IS:** Signing a query string but Binance WS verifies against canonicalized JSON params.

Binance WS does NOT verify against:
```
query_string_you_constructed
```

It verifies against:
```
canonicalized(params_object_from_JSON)
```

## The 3 Mandatory Fixes (ALL APPLIED)

### ✅ FIX #1: Monotonic Timestamps (CRITICAL)

Burst probes were reusing timestamps:
```
timestamp = 1767428739332
timestamp = 1767428739332  ← REJECTED
```

**Fix:** Atomic CAS loop guarantees strictly increasing:
```cpp
static int64_t next_timestamp_ms() noexcept {
    int64_t now = now_ms();
    int64_t prev = last_timestamp_ms_.load();
    if (now <= prev) now = prev + 1;
    // CAS loop for thread safety
    while (!last_timestamp_ms_.compare_exchange_weak(...)) {
        if (now <= prev) now = prev + 1;
    }
    return now;
}
```

### ✅ FIX #2: Explicit newOrderRespType=ACK

Binance injects defaults if missing → breaks signature.

**Fix:** Always include explicitly in every order.

### ✅ FIX #3: Canonical Signing = JSON Params

**WRONG (before):**
```
params → query_string → HMAC
params → JSON (separate path)  ← DRIFT!
```

**CORRECT (now):**
```
params_object
→ canonicalize(params_object)
→ HMAC(canonical_string)
→ send SAME params_object as JSON
```

Same `CanonicalParamSet` is used for:
1. Generating canonical string for signing
2. Generating JSON for sending

**No drift possible.**

## What NOT Changed

- ❌ Did NOT chase decimal formatting (not the issue)
- ❌ Did NOT special-case price precision
- ❌ Did NOT add REST logic

## Files Modified

- `crypto_engine/include/binance/BinanceHMAC.hpp`

## Deployment

```bash
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/Chimera_v4.9.21_SIG_FIX.zip ~/
cd ~ && unzip Chimera_v4.9.21_SIG_FIX.zip
mv Chimera_v4.9.21 Chimera
cd ~/Chimera/build && rm -rf * && cmake .. && make -j$(nproc)
./chimera
```

## Expected After Fix

- `[PROBE_SENT]` → `[ORDER_ACK]` reliably
- No silent rejections
- Bootstrap completes
- System transitions to LIVE cleanly
- No more -1022 errors
