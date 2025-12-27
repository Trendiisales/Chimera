# Chimera v4.3.1 - CRITICAL REGIME GATE FIX

## Release Date: 2025-12-27

## CRITICAL BUG FIX

### The Problem
Chimera v4.3.0 (and earlier) was trading during INIT/TRANSITION/TOXIC regimes, causing:
- 23 trades in 45 seconds with $0 PnL (churning)
- Wasted fees on zero-edge trades
- Trading before EMAs were built (INIT state)

### Root Cause
Line 643-647 in SymbolThread.hpp was MISSING regime check:
```cpp
// OLD (BROKEN):
bool gate_pass = is_shadow_eligible && 
                 shadow_direction != 0 &&
                 !shadow_position_open_ &&
                 AllowTradeHFT(...);  // <-- No regime check!
```

### The Fix
```cpp
// NEW (FIXED):
bool regime_ok = (current_regime_ == MarketRegime::STABLE);
bool gate_pass = is_shadow_eligible && 
                 regime_ok &&  // v4.3.1: MUST be STABLE regime
                 shadow_direction != 0 &&
                 !shadow_position_open_ &&
                 AllowTradeHFT(...);
```

### Trading Now BLOCKED During:
- `INIT` - EMAs not ready, edge calculation garbage
- `TRANSITION` - Market changing, unreliable signals
- `TOXIC` - Spreads blown out, adverse selection risk
- `UNKNOWN` - State not determined

### Trading Only ALLOWED During:
- `STABLE` - Clean microstructure, reliable edge

## Additional Changes

### Diagnostic Logging
- Debug output now shows `REGIME=` status every 100 ticks
- Easier to verify regime transitions

### Files Modified
- `crypto_engine/include/binance/SymbolThread.hpp` - Gate fix + logging
- `src/main_dual.cpp` - Version bump to v4.3.1

## Deploy Commands
```bash
# Kill running engine
kill -9 $(pgrep -f chimera)

# Archive existing
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)

# Transfer via RDP to C:\Chimera\, then:
cp /mnt/c/Chimera/Chimera_v4.3.1.zip ~/
cd ~ && unzip -o Chimera_v4.3.1.zip
cd ~/Chimera && mkdir -p build && cd build
cmake .. && make -j4

# Run
./chimera
```

## Verification
After starting, you should see in logs:
```
[HFT-BTCUSDT] t=1 REGIME=INIT spread=... (NO TRADES)
[HFT-BTCUSDT] t=100 REGIME=INIT spread=... (NO TRADES)
[HFT-BTCUSDT] t=500 REGIME=STABLE spread=... (trades can fire)
```

No trades should fire until REGIME=STABLE appears.
