# Shadow Execution Engine

**Purpose:** Realistic multi-leg pyramiding execution simulation  
**Status:** Production-ready (v4.19.0)  
**Mode:** Shadow (no broker hits) or Live (FIX orders)

---

## Quick Start

```cpp
#include "shadow/MultiSymbolExecutor.hpp"

// 1. Create executor
shadow::MultiSymbolExecutor executor;

// 2. Add symbols (shadow mode)
executor.addSymbol(shadow::getXauConfig(), shadow::ExecMode::SHADOW);
executor.addSymbol(shadow::getXagConfig(), shadow::ExecMode::SHADOW);

// 3. Feed ticks
shadow::Tick tick{2734.0, 2734.2, timestamp_ms};
executor.onTick("XAUUSD", tick);

// 4. Feed signals
shadow::Signal signal{shadow::Side::BUY, 2734.2, 0.85, 0.25};
executor.onSignal("XAUUSD", signal);

// 5. Check PnL
double pnl = executor.getTotalRealizedPnl();
```

---

## Architecture

```
MultiSymbolExecutor
  ├── SymbolExecutor (XAUUSD)
  ├── SymbolExecutor (XAGUSD)
  ├── SymbolExecutor (NAS100)
  └── SymbolExecutor (US30)

Each SymbolExecutor:
  - Independent state machine
  - Own position tracking
  - Own PnL accounting
  - Own risk limits
```

---

## State Machine

```
FLAT
  ↓ Signal meets entry thresholds
ENTERED
  ↓ Min hold expires (2-4s)
LOCKED
  ↓ Stop/TP/Reversal
EXITING
  ↓
COOLDOWN (5-10s)
  ↓
FLAT
```

**Key Points:**
- Cannot exit during ENTERED (prevents micro-chop)
- Cannot enter during COOLDOWN (prevents overtrading)
- Opposite signals ignored while in position

---

## Entry Conditions

Must satisfy ALL:
1. `regime == NORMAL` (not in cooldown)
2. `confidence >= 0.80` (global threshold)
3. `abs(momentum) >= base_entry_mom` (symbol-specific)

**Symbol Thresholds:**
- XAUUSD: 0.20
- XAGUSD: 0.25
- NAS100: 0.35
- US30: 0.40

---

## Pyramid Conditions

Must satisfy ALL:
1. Already in position (same direction)
2. `abs(momentum) >= pyramid_mom`
3. `price >= last_entry + price_improve`
4. `legs < max_legs`
5. `mae < max_add_mae` (MAE guard - don't add into losers)

**Example (XAUUSD):**
```
Long @ 2734.32
Price reaches 2734.92 (0.60+ pts)
Momentum >= 0.30
MAE < 0.50
Legs < 4
→ Add leg
```

---

## Exit Conditions

**Stop Hit (Price-Based):**
```cpp
if (side == BUY && price <= stop_price)  → EXIT
if (side == SELL && price >= stop_price) → EXIT
```

**Take Profit:**
```cpp
if (side == BUY && price >= tp_price)  → EXIT
if (side == SELL && price <= tp_price) → EXIT
```

**Reversal (Signal-Based with MAE Guard):**
```cpp
if (abs(momentum) >= reversal_mom 
    && side != position_side
    && mae > -0.25R)  → EXIT
```

**Key:** Stops and TPs are **price-based**. Reversals are signal-based but **MAE-gated**.

---

## Symbol Configurations

### XAUUSD (Gold)
```cpp
Max Legs:     4
Stop:         1.20 pts
Entry Mom:    0.20
Pyramid Mom:  0.30
Reversal Mom: 0.40
Min Hold:     2000 ms
Cooldown:     5000 ms
Slippage:     0.12 pts
```

### XAGUSD (Silver)
```cpp
Max Legs:     2  // Conservative - "mean reverting, violent"
Stop:         0.18 pts
Entry Mom:    0.25
Pyramid Mom:  0.30
Reversal Mom: 0.50
Min Hold:     3000 ms
Cooldown:     8000 ms
Slippage:     0.04 pts
```

### NAS100
```cpp
Max Legs:     3
Stop:         25.0 pts
Entry Mom:    0.35
Reversal Mom: 0.60
Min Hold:     4000 ms
Cooldown:     7000 ms
```

### US30
```cpp
Max Legs:     2
Stop:         25.0 pts
Entry Mom:    0.40
Reversal Mom: 0.70
Min Hold:     4000 ms
Cooldown:     9000 ms
```

---

## Fill Model (Shadow Mode)

**Entry:**
```cpp
fill_price = (side == BUY) 
    ? signal_price + slippage 
    : signal_price - slippage
```

**Exit:**
```cpp
exit_price = mid_price  // (bid + ask) / 2
```

**PnL Calculation:**
```cpp
leg_pnl = (side == BUY)
    ? (exit - entry) * size
    : (entry - exit) * size

total_pnl = sum(all_leg_pnl)
```

---

## MAE/MFE Tracking

Updated on every tick:
```cpp
double pnl = (side == BUY) ? (mid - entry) : (entry - mid);
mae = min(mae, pnl);  // Maximum Adverse Excursion
mfe = max(mfe, pnl);  // Maximum Favorable Excursion
```

**Used For:**
- Pyramid guards (don't add into losers)
- Reversal gates (allow reversal if mae > -0.25R)
- Performance analysis

---

## Switching to Live Mode

**Step 1:** Change mode for one symbol
```cpp
executor.addSymbol(shadow::getXauConfig(), shadow::ExecMode::LIVE);
```

**Step 2:** Wire order submission in `SymbolExecutor::submit()`
```cpp
void SymbolExecutor::submit(const Leg& leg, const char* tag) const {
    if (mode_ == ExecMode::LIVE) {
        fix_client.sendMarketOrder(cfg_.symbol, leg.side, leg.size);
    }
}
```

**Step 3:** Start small
- 0.10 lots
- 1 symbol
- Monitor for 1 day

---

## Common Issues

### "No trades for hours"
**Likely causes:**
- Cooldown too long → reduce cooldown_ms
- Entry threshold too high → reduce base_entry_mom
- Signals not reaching thresholds → check signal generation

### "Too many trades"
**Likely causes:**
- Min hold too short → increase min_hold_ms
- Cooldown too short → increase cooldown_ms
- Entry threshold too low → increase base_entry_mom

### "Never see pyramiding"
**Likely causes:**
- price_improve too high → signals not moving price enough
- pyramid_mom too high → momentum decays before add
- max_add_mae too strict → always hitting MAE guard

### "Timestamps mismatch errors"
**Fix:**
- Ensure tick timestamps match signal timestamps
- Both should come from same clock source

---

## Logging

**Entry:**
```
[SHADOW] XAUUSD BASE BUY @ 2734.32 size=1 stop=2733.12
[SHADOW] XAUUSD PYRAMID BUY @ 2735.12 size=1 stop=2733.12
```

**Exit:**
```
[SHADOW] EXIT XAUUSD reason=STOP @ 2733.00 pnl=-1.32
[XAUUSD] EXIT ALL: reason=STOP legs=2 pnl=-3.44 realized_total=-3.44
```

**State:**
```
[XAUUSD] COOLDOWN → NORMAL
```

---

## Performance Expectations

**v4.18.0 (old shadow):**
- 50-100+ trades/day
- Signal churn visible
- Unrealistic PnL

**v4.19.0 (new shadow):**
- 10-20 trades/day
- Longer holds
- Realistic PnL
- 60-80% reduction in trade count

---

## Testing

Run standalone test:
```bash
g++ -std=c++17 -I include \
    src/shadow/SymbolExecutor.cpp \
    src/shadow/MultiSymbolExecutor.cpp \
    tests/shadow_test.cpp -o test
./test
```

Expected output:
```
[SHADOW_INIT] XAUUSD mode=SHADOW max_legs=4
[SHADOW] XAUUSD BASE BUY @ 2734.32 size=1
[SHADOW] XAUUSD PYRAMID BUY @ 2735.12 size=1
[SHADOW] EXIT XAUUSD reason=STOP
TOTAL REALIZED PNL: -3.44
```

---

## References

- **CHANGELOG_v4.19.0.md** - Full release notes
- **Document 1-4** - Design documentation (in original docs)
- **ShadowTypes.hpp** - Core types and enums
- **ShadowConfig.hpp** - Symbol configurations

---

**Last Updated:** v4.19.0 (Feb 6, 2026)
