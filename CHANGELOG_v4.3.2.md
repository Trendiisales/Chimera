# Chimera v4.3.2 - WARMUP GATE FIX

## Release Date: 2025-12-27

## Why v4.3.1 Didn't Work

v4.3.1 added a regime check, BUT:
1. `current_regime_` was **initialized to STABLE** (line 1361)
2. On startup, regime_ok check passed immediately!
3. Trades fired during first few ticks before real regime computed

## Fixes in v4.3.2

### Fix 1: Default Regime → TRANSITION (not STABLE)
```cpp
// OLD (v4.3.1):
Chimera::Crypto::MarketRegime current_regime_ = MarketRegime::STABLE;

// NEW (v4.3.2):
Chimera::Crypto::MarketRegime current_regime_ = MarketRegime::TRANSITION;
```

Now on startup:
- Regime starts as TRANSITION
- regime_ok check FAILS until genuinely STABLE

### Fix 2: Minimum 500 Ticks Before ANY Trading
```cpp
bool warmup_complete = (tick_count_ >= 500);  // Must have 500+ ticks
bool regime_ok = (current_regime_ == MarketRegime::STABLE);
bool gate_pass = is_shadow_eligible && 
                 warmup_complete &&  // NEW: 500-tick minimum
                 regime_ok &&        // Must be STABLE
                 shadow_direction != 0 &&
                 !shadow_position_open_ &&
                 AllowTradeHFT(...);
```

### Enhanced Logging
Debug output now shows:
```
[HFT-BTCUSDT] t=100 WARMUP=WAIT REGIME=TRANSITION spread=...
[HFT-BTCUSDT] t=500 WARMUP=OK REGIME=STABLE spread=...
```

## Files Modified
- `crypto_engine/include/binance/SymbolThread.hpp`:
  - Line 1361: Default regime → TRANSITION
  - Line 646-653: Added warmup_complete check
  - Debug logging: Added WARMUP status

- `src/main_dual.cpp`:
  - Version bump to v4.3.2

## Trading is Now BLOCKED Until:
1. ✓ 500+ ticks received (warmup_complete)
2. ✓ Regime == STABLE (not TRANSITION/TOXIC)
3. ✓ Bootstrap complete (information-based)
4. ✓ All AllowTradeHFT checks pass

## Deploy
```bash
# Kill old
kill -9 $(pgrep -f chimera)

# Archive
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)

# Transfer zip via RDP, then:
cp /mnt/c/Chimera/Chimera_v4.3.2.zip ~/
cd ~ && unzip -o Chimera_v4.3.2.zip
mkdir -p ~/Chimera && mv CHANGELOG* crypto_engine cfd_engine src include config* CMakeLists.txt README.md ml scripts docs chimera_dashboard.html ~/Chimera/

# Build
cd ~/Chimera && mkdir -p build && cd build
cmake .. && make -j4

# Run
./chimera
```

## Verification
Logs should show NO TRADES until:
```
[HFT-BTCUSDT] t=500 WARMUP=OK REGIME=STABLE ...
▶▶  ENTRY  BTCUSDT  LONG  @...  (first trade after warmup)
```

## Additional Fixes (Update 2)

### Fix 3: GUI Symbol Enable Check
The GUI's symbol selection was not being enforced by the trading engine.

Added:
- `include/shared/SymbolEnabledManager.hpp` - Global atomic symbol enable flags
- GUIBroadcaster now updates both TradingConfig AND SymbolEnabledManager
- SymbolThread checks `isSymbolTradingEnabled()` before every trade

Gate now requires ALL of:
1. ✓ Symbol enabled in GUI
2. ✓ 500+ ticks received (warmup)
3. ✓ Regime == STABLE
4. ✓ Bootstrap complete
5. ✓ AllowTradeHFT checks pass

### Fix 4: Compiler Warning
Fixed `-Wunused-variable` warning in BootstrapEvaluator.hpp (prev_state)

### Debug Logging
Now shows `EN=Y/N` for symbol enabled status:
```
[HFT-BTCUSDT] t=100 EN=N WARMUP=WAIT REGIME=TRANSITION ...  (disabled)
[HFT-BTCUSDT] t=100 EN=Y WARMUP=WAIT REGIME=TRANSITION ...  (enabled but waiting)
[HFT-BTCUSDT] t=500 EN=Y WARMUP=OK REGIME=STABLE ...        (can trade)
```

### IMPORTANT: Default Behavior
All symbols start DISABLED. You must:
1. Select symbols in GUI
2. Click APPLY
3. Then symbols become tradeable
