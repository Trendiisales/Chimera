# Chimera v4.3.4 - STATE==RUNNING Check (THE KEY FIX)

## Root Cause Found!

The crypto engine's `SymbolThread` has a `state_` variable that tracks:
```cpp
enum class SymbolState : uint8_t {
    INIT = 0, WAITING = 1, SYNCING = 2, RUNNING = 3, STOPPED = 4, ERROR = 5
};
```

`state_` starts at `INIT` and only becomes `RUNNING` after the order book snapshot is applied (line 246).

**THE BUG:** The trading gate did NOT check `state_ == RUNNING`. So trades could fire while still in INIT/WAITING states!

## The Fix

Added `state_running` check to gate_pass:
```cpp
bool state_running = (state_ == SymbolState::RUNNING);  // v4.3.4: THE KEY CHECK
bool gate_pass = is_shadow_eligible && 
                 state_running &&    // v4.3.4: MUST be RUNNING state
                 symbol_enabled &&   // v4.3.2: Must be enabled in GUI
                 warmup_complete &&  // v4.3.2: Must have 500+ ticks
                 regime_ok &&        // v4.3.1: MUST be STABLE regime
                 ...
```

Also added to HARD BLOCK for defense in depth.

## Debug Output

Now shows `ST=RUN` or `ST=INIT`:
```
[HFT-BTCUSDT] t=100 ST=INIT EN=Y WARMUP=WAIT ...  (will NOT trade)
[HFT-BTCUSDT] t=500 ST=RUN EN=Y WARMUP=OK REGIME=STABLE ...  (CAN trade)
```

## All Gates Now Required

Trading requires ALL of:
1. ✓ `state_ == RUNNING` (order book snapshot received)
2. ✓ `symbol_enabled` (GUI selection)
3. ✓ `tick_count_ >= 500` (warmup)
4. ✓ `regime == STABLE` (not TOXIC/TRANSITION)
5. ✓ `bootstrap_complete` (information-based)
6. ✓ AllowTradeHFT checks pass

## Files Modified
- `crypto_engine/include/binance/SymbolThread.hpp` - Added state_running check to gate_pass + HARD BLOCK
- `src/main_dual.cpp` - Version bump
