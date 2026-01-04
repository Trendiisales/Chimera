# Chimera v4.9.9 - CRITICAL FIX: Trade Callback on Fill Only

## Date: 2026-01-02

## ROOT CAUSE OF BUY SPAM FIXED

The infinite BUY spam loop was caused by `trade_cb_` firing on ORDER SUBMISSION instead of FILL:

### The Problem (v4.9.8 and earlier)
```
1. Signal detected → enter() called
2. Order sent to exchange
3. trade_cb_ fires IMMEDIATELY → dashboard shows "BUY"
4. pending_fill_ = true, state stays FLAT
5. Maker order never fills (MAKER_TIMEOUT)
6. pending_fill_ = false, cooldown reset
7. 500ms later → same signal → enter() again
8. REPEAT FOREVER → infinite BUY spam, no SELLs
```

### The Fix (v4.9.9)
```
1. Signal detected → enter() called
2. Order sent to exchange
3. orders_submitted_++ (track attempts)
4. For MAKER: pending_fill_ = true, NO trade_cb_ yet
5. Wait for exchange fill...
6a. FILL received → onFill() → trade_cb_ fires → trades_entered_++
6b. TIMEOUT → abort, reset, cooldown → NO trade_cb_ (no position)
7. Dashboard only shows ACTUAL fills, not order attempts
```

## Changes

### CryptoMicroScalp.hpp
1. **`trade_cb_` moved to `onFill()` for maker orders**
   - Only fires when exchange confirms fill
   - Taker orders still fire immediately (instant fill assumed)

2. **New counters for debugging**
   - `orders_submitted_`: Total order attempts (success + timeout)
   - `maker_timeouts_`: How many maker orders timed out
   - `trades_entered_`: Only incremented on actual fills

3. **`order_submit_ts_ns_` timestamp**
   - Tracks when order was submitted (wall clock)
   - Used for accurate timeout calculation

4. **Wall clock cooldown**
   - Cooldown now uses `steady_clock::now()`, not tick timestamp
   - Prevents clock skew issues

5. **EntrySnapshot stores qty**
   - `snapshot_.qty` preserved for fill callback
   - Exit uses stored qty instead of recalculating

6. **Enhanced logging**
   - Shows orders/fills/timeouts in periodic log
   - Clear distinction between ORDER_SUBMITTED vs FILL

## Console Output Changes

**Old (v4.9.8):**
```
[MICROSCALP][BTCUSDT] ENTER gross_edge=2.10 qty=0.000300 → PROBE
```

**New (v4.9.9):**
```
[MICROSCALP][BTCUSDT] ORDER_SUBMITTED (MAKER) gross_edge=2.10 qty=0.000300 → WAITING_FOR_FILL
[MICROSCALP][BTCUSDT] FILL: MAKER @ 88456.50 → PROBE (timeout=220ms) trades=1
```
or
```
[MICROSCALP][BTCUSDT] ORDER_SUBMITTED (MAKER) gross_edge=2.10 qty=0.000300 → WAITING_FOR_FILL
[MICROSCALP][BTCUSDT] MAKER_TIMEOUT: age=221ms timeout=220ms, ABORTING (orders=1 fills=0 timeouts=1)
```

## Dashboard Impact

- BUY signals now only appear when orders actually FILL
- No more phantom trades from unfilled maker orders
- `orders_submitted` vs `trades_entered` shows fill rate

## Upgrade Path

1. Kill existing Chimera process
2. Archive current: `mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)`
3. Unzip v4.9.9 and rebuild
4. Re-enable crypto symbols in dashboard

## Verification

After startup, watch for:
```
[MICROSCALP] Created BTCUSDT v4.9.9 (TRADE_CB ON FILL ONLY)
```

If maker orders timeout, you should see:
```
MAKER_TIMEOUT: age=XXXms timeout=220ms, ABORTING
```
NOT repeated BUY entries.
