# CHIMERA v6.73 CHANGELOG - GUI FIX + RELAXED SCALPER

## Release Date: December 23, 2024

## Critical Fixes

### 1. GUI FREEZE FIX
**Problem:** Dashboard would freeze/stop updating after a while
**Cause:** Blocking `send()` calls in broadcast_loop - if any WebSocket client slows down (browser tab backgrounded, network hiccup), it blocks the entire broadcast thread
**Fix:** 
- Added `poll()` with 10ms timeout before each send
- If socket not ready within timeout, disconnect the client instead of blocking
- Uses non-blocking `MSG_DONTWAIT` flag on Linux, `select()` on Windows

### 2. NO TRADES - ENTRY THRESHOLDS TOO STRICT
**Problem:** Scalper never entered trades, always showing "NO_SIGNAL" or "NO VOTES"
**Cause:** Required ALL THREE conditions: `trend != 0 && trend == mom && spreadTight()`
**Fix:**
- Changed from AND to OR logic: `(trend != 0 || mom != 0) && spreadTight()`
- Lowered trend threshold from 0.5 ATR to 0.3 ATR
- Lowered momentum threshold from 0.3 ATR to 0.15 ATR
- Relaxed spread tight check from 1.2x to 1.5x average
- Reduced warmup from 20 ticks to 10 ticks

### 3. BLOCK REASON NOT DISPLAYING
**Problem:** GUI showed generic "NO VOTES" instead of actual scalper reason
**Cause:** JavaScript was computing its own block reason, ignoring `d.market_state.reason` from C++
**Fix:**
- Now uses actual scalper reason from `d.market_state.reason`
- Added BIG prominent display showing the actual reason
- Color coded: blue=warmup, cyan=in position, orange=spread, yellow=no signal, green=trading
- Added detailed explanation of each block reason
- Connected to `last-suppression` element in bring-up panel

## Files Changed

1. **GUIBroadcaster.hpp**
   - Non-blocking broadcast_loop with poll() timeout
   - Added `<cerrno>` include for error handling

2. **PureScalper.hpp**
   - OR logic for entry (trend OR momentum instead of AND)
   - Lowered thresholds for more signals
   - Reduced warmup to 10 ticks
   - Better block reason strings

3. **chimera_dashboard.html**
   - New big prominent BLOCK REASON display (text-2xl)
   - Uses actual scalper reason from C++
   - Color coded with detailed explanations
   - Updated last-suppression element

4. **CfdEngine.hpp**
   - Logs every block reason change to debug file
   - More verbose periodic logging (every 200 ticks instead of 500)
   - Added EURUSD to diagnostic symbols

5. **main_dual.cpp**
   - Updated version to v6.73
   - Added fix summary in startup banner

## New Scalper Entry Thresholds

| Setting | v6.72 (Old) | v6.73 (New) |
|---------|-------------|-------------|
| Trend threshold | 0.5 ATR | 0.3 ATR |
| Momentum threshold | 0.3 ATR | 0.15 ATR |
| Spread tight | 1.2x avg | 1.5x avg |
| Warmup ticks | 20 | 10 |
| Entry logic | AND (both) | OR (either) |

## Debug Log Location

All scalper activity is logged to:
```
~/Chimera/build/chimera_debug.log
```

Watch in real-time:
```bash
tail -f ~/Chimera/build/chimera_debug.log
```

Look for:
- `REASON_CHANGE` - Every time block reason changes
- `PERIODIC` - Regular status updates
- `SIGNAL` - Trade signals generated
- `TRADE` - Actual trades executed
- `BLOCK` - Why trades were blocked

## Deploy Commands

```bash
# On VPS via WSL
mv ~/Chimera ~/Chimera_backup_$(date +%Y%m%d_%H%M%S)
cd ~ && unzip /mnt/c/Chimera/Chimera_v6.73_SCALPER.zip
mv Chimera_v6.73 Chimera
cd ~/Chimera && mkdir -p build && cd build
cmake .. && make -j4
./chimera
```

Watch debug log:
```bash
tail -f ~/Chimera/build/chimera_debug.log
```
