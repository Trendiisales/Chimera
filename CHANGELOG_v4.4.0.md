# Chimera v4.4.0 - Triple Engine Architecture

## Critical Changes (This Update)

### LOCAL STAND-DOWN (NEW - MOST IMPORTANT)
Behavior-based circuit breaker that ML cannot replace:
- **Trigger:** 2 consecutive FAILED trades within 20 minutes
- **Action:** NAS100 income DISABLED for 45 minutes
- **No overrides, no exceptions**

A trade is a FAIL if:
- Hit STOP_LOSS
- Exited via TIMEOUT with negative PnL

NOT a fail: scratch, TP, timeout with ≥0 PnL

This prevents:
- Slow absorption grinds
- Death by scratches
- Emotional interference
- "Just one more try" logic
- Invisible expectancy bleed

### ML Veto Visibility (MANDATORY)
- Every ML veto is logged with full details - NO SUPPRESSION
- Veto log includes: score, threshold, vol_pct, compression_ratio, spread_instability, impulse_rate
- Session stats track: ml_vetoes, spread_vetoes, liquidity_vetoes separately
- GUI helper: `idle_reason()` returns "ML veto (score 0.52)" not silence

### Symbol Lock (HARD ENFORCED)
- **NAS100 ONLY** - Income engine rejects all other symbols
- **XAUUSD HARD DISABLED** - Gold tempts on quiet days, that temptation is lethal
- No symbol additions without 20+ sessions proof

### MAE/MFE Tracking (MANDATORY)
- Every trade records Max Adverse Excursion and Max Favorable Excursion
- Session stats include: total_mae_bps, total_mfe_bps
- Trade record prints: `PnL=2.50bps MAE=-1.20bps MFE=3.10bps exit=TP`

### Exit Reason Tracking
- All exits categorized: TP, SL, TRAIL, TIME, KILL, VETO, HARDFAIL
- Stats track: exits_tp, exits_sl, exits_trail, exits_time

### Hard Fail Conditions (AUTO-STOP)
- Daily DD > -0.50% → HALT
- >10 trades in session → HALT (boredom breach = logic leak)
- `halt_on_hard_fail = true` enforced

### Boredom Threshold
- 2-6 trades/day = healthy
- 0 trades = acceptable  
- 10+ trades = RED FLAG → auto-halt

### Session Window Blocks
- London open (08:00 UTC) → BLOCKED
- NY open (13:30-14:30 UTC) → BLOCKED
- Only trade AFTER opens settle

### ML Threshold Lock
- `ml_veto_threshold = 0.60` - FIXED, not dynamic
- `ml_failure_vetoes_all = true` - safe default
- No adaptive learning, no online retraining

## Success Criteria (10 London Sessions)

### AUTOMATIC FAIL (Stop Immediately)
- ❌ Daily drawdown > -0.50%
- ❌ >10 trades in single session
- ❌ Trades during London/NY open
- ❌ ML veto overridden manually

### PASS CONDITIONS (All Required)
- ✅ Max daily DD ≤ -0.50%
- ✅ Median trades/day ≤ 4
- ✅ 30-50% of sessions have 0 trades
- ✅ ML vetoes occur regularly
- ✅ IncomeEngine PnL variance < Alpha variance

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    CHIMERA v4.4 TRIPLE ENGINE                        │
├─────────────────┬───────────────────────────┬───────────────────────┤
│ ENGINE 1        │ ENGINE 2                  │ ENGINE 3              │
│ Binance (Crypto)│ cTrader (CFD)             │ Income (ML-Filtered)  │
│ Alpha Trades    │ Alpha Trades              │ NAS100 ONLY           │
├─────────────────┴───────────────────────────┴───────────────────────┤
│                    SHARED (ATOMICS ONLY)                             │
│                 GlobalKill + DailyLossGuard                          │
└─────────────────────────────────────────────────────────────────────┘
```

## Files
- `income_engine/include/IncomeEngine.hpp` - Complete rewrite with all tracking
- `income_engine/include/IncomeRegimeFilter.hpp` - ML regime filter
- `src/main_triple.cpp` - NAS100 only wiring

## Build
```bash
mkdir build && cd build
cmake ..
make chimera_triple
./chimera_triple
```
