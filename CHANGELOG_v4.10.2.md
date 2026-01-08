# CHANGELOG v4.10.2 - PRODUCTION LOCKDOWN

## Date: 2025-01-06

## Summary
Complete lockdown for controlled live deployment. Two symbols, fixed risk, frozen mode, proper exit engine.

---

## 🔒 PHASE 1: SYMBOL LOCK

### What Changed
Only NAS100 and US30 are allowed anywhere in the execution path.

### Startup Log
```
[SYMBOLS] Registered: NAS100, US30
[SYMBOLS] Total: 2 symbols (v4.10.2 lock active)
[SYMBOLS] REJECTED (v4.10.2 lock):
[SYMBOLS]   - US100 (use NAS100 instead)
[SYMBOLS]   - SPX500
[SYMBOLS]   - EURUSD, GBPUSD, USDJPY (FX disabled)
[SYMBOLS]   - XAUUSD (Gold disabled)
```

### Implementation
- `isAllowedSymbol()` function checks for NAS100/US30 only
- `CfdEngineIntegration::init()` only registers these two symbols
- Non-allowed symbols return `SYMBOL_NOT_ALLOWED` immediately

---

## 🔒 PHASE 2: ENGINE OWNERSHIP (E2 PRIMARY)

### What Changed
Engine 2 (VWAP pullback) is now the primary and only trade executor.
Engine 1 (compression) is a filter only - cannot open trades independently.

### E2 (Primary Executor)
- Opens positions
- Controls direction
- Controls timing
- Entry: VWAP pullback with OR structure

### E1 (Filter Only)
- ❌ Cannot open trades
- ✅ Provides compression quality score (0-1)
- ✅ Improves E2 confidence when compression detected

### Trade Log
```
[INDEX-E2] NAS100 ENTRY LONG @ 18250.50 SL=18200.00 risk=0.50% size=0.0100
```

You will NEVER see:
```
[INDEX-E1] Trade opened   ← IMPOSSIBLE
```

---

## 🔒 PHASE 3: FIXED RISK

### What Changed
Risk is constant, boring, predictable. No scaling of any kind.

| Symbol | Risk |
|--------|------|
| NAS100 | 0.50% |
| US30 | 0.40% |

### What Is Disabled
- ❌ Volatility scaling
- ❌ Performance scaling
- ❌ Engine health scaling
- ❌ Portfolio rebalancing

### Order Log
```
[RISK] NAS100 risk=0.50%
```
Same number every time.

### Implementation
```cpp
static constexpr double NAS100_RISK = 0.005;  // 0.5% fixed
static constexpr double US30_RISK = 0.004;    // 0.4% fixed
```

---

## 🔒 PHASE 4: EXIT ENGINE

This is where money is made (or preserved).

### 4.1 Partial Profit
- Fires at structural level (OR high/low)
- Takes 60% of position
- Minimum +1R before partial

**Log:**
```
[INDEX-E2] NAS100 PARTIAL @ 18300.00 size=0.0060 PnL=1.25R
```

### 4.2 Stall Kill
- If trade makes no progress after 6 bars
- Closes trade before hitting full SL
- Prevents slow bleed

**Log:**
```
[INDEX-E2] NAS100 EXIT STALL @ 18245.00 bars=6 PnL=-0.10R reason=NO_PROGRESS
```

### 4.3 Runner Protection
- After partial: stop moves to BE + buffer
- Runner cannot fully reverse
- Trailing activates at +2R

**Log:**
```
[INDEX-E2] NAS100 Runner protected: stop moved to 18255.00 (BE+buffer)
```

### Exit Stages
1. `INITIAL` - Full position, initial stop
2. `PARTIAL_TAKEN` - 60% taken, runner active
3. `RUNNER_PROTECTED` - Stop at BE
4. `TRAILING` - Aggressive trail on runner

---

## 🔒 PHASE 5: PORTFOLIO FREEZE

### What Changed
- Mode is frozen to `INDEX_PRIORITY`
- No mode switching
- No reallocation
- Daily loss halt at -2R

### Startup Log
```
[PORTFOLIO] Mode locked: INDEX_PRIORITY
[PORTFOLIO] Risk: NAS100=0.5% US30=0.4% (FIXED)
[PORTFOLIO] Daily loss limit = -2.0R
```

### Implementation
```cpp
PortfolioMode mode = PortfolioMode::INDEX_PRIORITY;
bool mode_locked = true;  // v4.10.2: Always locked
```

---

## Files Changed

### `/include/engines/IndexImpulseEngine.hpp`
- Restructured: E2 primary, E1 filter only
- Added exit engine with partial/stall/runner logic
- Symbol lock to NAS100/US30
- Fixed risk (no scaling)

### `/include/portfolio/PortfolioModeController.hpp`
- Simplified: INDEX_PRIORITY only
- Symbol lock enforcement
- Fixed risk constants
- Daily halt at -2R

### `/include/integration/CfdEngineIntegration.hpp`
- Only registers NAS100 and US30
- Proper logging for risk/symbols
- Rejects all other symbols

---

## Expected Runtime Behavior

### Trade Opening
```
[INDEX-E2] NAS100 E2 eligible: ATR=25.50 compression=YES (0.65)
[INDEX-E2] Entry signal: LONG setup=LONG compression=YES(0.65)
[INDEX-E2] NAS100 ENTRY LONG @ 18250.50 SL=18200.00 risk=0.50% size=0.0100 partial_target=18300.00
[RISK] NAS100 risk=0.50%
```

### Partial Exit
```
[INDEX-E2] NAS100 PARTIAL @ 18300.00 size=0.0060 PnL=1.25R
[INDEX-E2] NAS100 Runner protected: stop moved to 18255.00 (BE+buffer)
```

### Stall Kill
```
[INDEX-E2] NAS100 EXIT STALL @ 18245.00 bars=6 PnL=-0.10R reason=NO_PROGRESS
```

### Stop Loss
```
[INDEX-E2] NAS100 EXIT SL @ 18200.00 PnL=-1.00R exit_stage=INITIAL
```

### Daily Halt (if triggered)
```
[PORTFOLIO] DAILY HALT: Loss=2.00R exceeds limit=2.0R
```

---

## Verification Checklist

### Phase 1: Symbol Lock
- [ ] Only NAS100 and US30 in startup logs
- [ ] No US100, SPX500, FX, Gold

### Phase 2: Engine Ownership
- [ ] All trades show `[INDEX-E2]`
- [ ] No `[INDEX-E1]` trade logs

### Phase 3: Fixed Risk
- [ ] NAS100 always shows `risk=0.50%`
- [ ] US30 always shows `risk=0.40%`
- [ ] Risk never changes

### Phase 4: Exit Engine
- [ ] Partial exits occur at structural levels
- [ ] Stall kills occur (not all losers hit SL)
- [ ] Runner protection logged after partial

### Phase 5: Portfolio Freeze
- [ ] Mode logged as `INDEX_PRIORITY` at startup
- [ ] Mode never changes during session
- [ ] Daily halt logged but not triggered (normal operation)

---

## Future Re-enablement

To add more symbols (after proving current setup works):
1. Add symbol to `isAllowedSymbol()` function
2. Add fixed risk constant
3. Register in `CfdEngineIntegration::init()`
4. Test in shadow before live

To enable adaptive risk (after proving fixed risk works):
1. Add scaling logic to `getRisk()`
2. Keep base risk as floor
3. Cap at 1.5x base max
4. Log scaling factor
