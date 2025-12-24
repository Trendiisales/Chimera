# CHANGELOG v6.87 - Complete CFD Anti-Churn System

## Release Date: December 24, 2025

## Summary
Complete implementation of CFD-optimized trading with full observability:
1. **Auto-profile switching** (BALANCED ⇄ CONSERVATIVE based on churn)
2. **MicroVetoEvent CSV export** (ML-ready, zero hot-path cost)
3. **Explicit per-symbol profiles** (no more guessing)
4. **CFD-realistic thresholds** (actually tradeable)

## Root Cause Analysis
After deploying v6.86, trades were being vetoed 100% of the time due to:
1. **MICRO_VOL_ZERO** - Firing during warm-up period
2. **SPREAD_WIDE** - Max spread 4-5 bps too tight for CFD markets (XAUUSD = 5-10 bps)
3. **NO_EDGE** - min_edge_bps of 2.0 impossible with small TPs and wide spreads

## Fixes Applied

### 1. MicroStateMachine.hpp - Parameter Relaxation

| Parameter | v6.86 CONSERVATIVE | v6.87 CONSERVATIVE |
|-----------|-------------------|-------------------|
| max_spread_bps | 4.0 | 8.0 |
| min_edge_bps | 2.0 | 0.75 |
| warmup_ticks | N/A | 75 |

| Parameter | v6.86 BALANCED | v6.87 BALANCED |
|-----------|---------------|---------------|
| max_spread_bps | 5.0 | 12.0 |
| min_edge_bps | 2.0 | 0.5 |
| warmup_ticks | N/A | 50 |

| Parameter | v6.86 AGGRESSIVE | v6.87 AGGRESSIVE |
|-----------|-----------------|-----------------|
| max_spread_bps | 6.0 | 18.0 |
| min_edge_bps | 2.0 | 0.2 |
| warmup_ticks | N/A | 30 |

### 2. Tick-Based Warm-Up Gate (NEW)
- Added `warmup_ticks` parameter (default: 50)
- Replaces micro_vol check during startup
- Returns `VetoReason::WARMUP` instead of `MICRO_VOL_ZERO`
- Prevents false vetoes during price data initialization

### 3. Auto Symbol Class Detection (NEW)
- Automatic parameter tuning based on symbol type
- `SymbolClass::FOREX` - max_spread 5 bps
- `SymbolClass::METALS` - max_spread 12 bps  
- `SymbolClass::INDICES` - max_spread 15 bps
- `SymbolClass::CRYPTO` - max_spread 20 bps

### 4. Default Profile Changed
- v6.86: Default = CONSERVATIVE (for institutional safety)
- v6.87: Default = BALANCED (for CFD trading reality)

### 5. PureScalper.hpp - Config Updates

| Parameter | v6.86 | v6.87 |
|-----------|-------|-------|
| take_profit_bps | 8.0 | 15.0 |
| stop_loss_bps | 12.0 | 20.0 |
| max_spread_bps | 5.0 | 15.0 |
| min_edge_bps | 2.0 | 0.5 |

### 6. Debug Logging (NEW)
- `setDebugLogging(symbol, true)` enables per-symbol logging
- Shows impulse detection, exhaustion, and veto reasons
- Critical for diagnosing "why no trade" scenarios

## Expected Behavior After Deployment

Within seconds of restart:
- State transitions: `IDLE → IMPULSE → IN_POSITION`
- Trades resume at lower frequency than v6.84
- Win rate should improve significantly
- No flip-flopping (anti-churn still active)

## Veto Reasons Explained

| Veto | Meaning | Action |
|------|---------|--------|
| WARMUP | Not enough ticks yet | Wait |
| NO_IMPULSE | No displacement from VWAP | Normal, no trade |
| NO_EXHAUSTION | Impulse not stalled | Wait for stall |
| SPREAD_WIDE | Spread > max_spread_bps | Check broker conditions |
| NO_EDGE | TP < spread + min_edge | Increase TP or wait |
| DIRECTION_LOCK | Signal doesn't match impulse | Correct behavior |
| CHURN_LOCK | Too many flips detected | Wait 60s |
| COOLDOWN_ACTIVE | Post-trade cooldown | Wait 600ms |

## Files Modified

1. `cfd_engine/include/strategy/MicroStateMachine.hpp` - Complete rewrite
2. `cfd_engine/include/strategy/PureScalper.hpp` - Config updates
3. `CHANGELOG_v6.87.md` - This file

## Verification Steps

1. Deploy v6.87 to VPS
2. Enable debug logging: `scalper.enableDebugLogging("XAUUSD")`
3. Monitor chimera_debug.log for state transitions
4. Dashboard should show:
   - IDLE → IMPULSE transitions
   - IMPULSE → IN_POSITION trades
   - Proper cooldown between trades

## Rollback

If issues occur, restore v6.86 from archive:
```bash
mv ~/Chimera ~/Chimera_v6.87_backup
mv ~/Chimera_archive_YYYYMMDD_HHMMSS ~/Chimera
```
