# CHANGELOG v4.3 - Crypto Burst Engine + BlackBull CFD Configuration

## Version 4.3.0

### Part A: Crypto Opportunistic Burst Engine

### New Module: crypto_engine/include/burst/

Added a completely new trading module implementing the **Opportunistic Burst Strategy** for crypto assets. This is fundamentally different from the existing market-making scalping engine.

### Files Added

```
crypto_engine/include/burst/
├── CryptoBurstEngine.hpp      # Main engine (~900 lines)
├── BurstBinanceAdapter.hpp    # Binance WebSocket integration (~300 lines)
└── README.md                  # Operating documentation

src/
└── burst_test.cpp             # Unit test / demo
```

### Key Features

#### Pre-Gate System (ALL Must Pass)
- **Volatility expansion**: realized vol ≥ 2.0× trailing 30-min median
- **Spread compression**: spread ≤ p25 of last 30 minutes
- **Book imbalance**: top-10 liquidity imbalance ≥ 65/35
- **Displacement**: price move ≥ 6 ticks
- **Regime**: confirmed TRENDING (not TRANSITION or RANGING)
- **Edge requirement**: edge ≥ 3× total cost

#### Entry Rules
- One direction only (following imbalance)
- Single entry (NO scaling-in, EVER)
- Taker execution (WebSocket API only, not REST)

#### Exit Rules
- Time stop: 5-30 seconds (hard limits)
- Structure break: imbalance collapses to ~50/50
- Max adverse: 0.5R excursion (tight)
- Cooldown: 5 minutes (win) / 15 minutes (loss)

#### Symbol Configuration
| Symbol | Mode | Status |
|--------|------|--------|
| BTCUSDT | LIVE | Primary - production enabled |
| ETHUSDT | SHADOW | Paper trading only |
| SOLUSDT | SHADOW | Paper trading only |

### Operating Contract

**SUCCESS METRIC**: RARE, high-expectancy wins; ZERO bleed otherwise.

Expected behavior:
- Days with 0 trades: NORMAL and CORRECT
- 1-3 trades per week: OPTIMAL
- >5 trades per week: INVESTIGATE - gate compromised

### Observability

Idle state logging every 60 seconds:
```
[CRYPTO] OFF — vol=1.3x(LOW) spread=p45(WIDE) imbal=52/48(WEAK) disp=3t(LOW) regime=RANGING(BLOCKED) cd=0s
```

### Integration

```cpp
#include "burst/CryptoBurstEngine.hpp"
#include "burst/BurstBinanceAdapter.hpp"

auto burst_adapter = chimera::crypto::burst::create_btc_burst_adapter(order_sender);
burst_adapter->engine()->start();

// Feed from existing Binance WebSocket callbacks
burst_adapter->on_depth_update(symbol, bids, asks, exchange_ts);
burst_adapter->on_agg_trade(symbol, price, qty, is_buyer_maker, ts);
```

### Configuration Defaults

```cpp
BurstGateConfig:
  vol_expansion_min = 2.0
  spread_percentile_max = 25.0
  imbalance_ratio_min = 0.65
  displacement_ticks_min = 6
  required_regime = TRENDING
  edge_to_cost_min = 3.0

BurstExitConfig:
  time_stop_min_sec = 5
  time_stop_max_sec = 30
  max_adverse_r = 0.5
  structure_break_exit = true
  imbalance_collapse_threshold = 0.50

BurstCooldownConfig:
  cooldown_after_win_sec = 300
  cooldown_after_loss_sec = 900

Daily Limits:
  daily_loss_limit_usd = 100.0
  max_daily_trades = 5
```

### Testing

```bash
# Compile test
g++ -std=c++17 -I. -Icrypto_engine/include -o burst_test src/burst_test.cpp

# Run test
./burst_test
```

### Design Philosophy

> "Crypto is OFF by default. It turns ON only when ALL pre-gate conditions are 
> simultaneously met. There is no tuning to see more trades."

> "Silence is intentional. When idle, the engine logs WHY it's idle.
> No trade = system protecting capital, NOT failing."

### NEVER DO

❌ Relax pre-gate conditions to "see action"  
❌ Add symbols beyond the approved list  
❌ Re-enter during cooldown  
❌ Scale into positions  
❌ Override daily limits  

---

## Version 4.2.4 (Previous)

See CHANGELOG_v4.2.md

---

## Part B: BlackBull CFD Configuration (Session-Aware)

### New Module: cfd_engine/include/config/

Added comprehensive BlackBull-tuned configuration modules for CFD trading.

### Files Added

```
cfd_engine/include/config/
├── BlackBullSpreadTables.hpp   # Session-aware spread gates (~400 lines)
├── NewsFilter.hpp              # High-impact news blocking (~400 lines)
├── CapitalScaling.hpp          # Position sizing & risk (~350 lines)
└── BlackBullConfig.hpp         # Unified gate interface (~200 lines)
```

### 1️⃣ Session-Aware Spread Tables (EXACT)

**Rule:** Spreads are session-aware. Outside preferred windows → BLOCK (not relax).

#### NAS100
| Session | Max Spread | Permission |
|---------|------------|------------|
| London Open | 1.0 pts | ✅ Allowed |
| London→NY | 1.1 pts | ✅ Allowed |
| NY Open | 1.1 pts | ✅ Allowed |
| NY Mid | 1.3 pts | ⚠️ Reduced |
| Asia/Off | — | ❌ Blocked |

#### SPX500
| Session | Max Spread | Permission |
|---------|------------|------------|
| London→NY | 0.9 pts | ✅ Allowed |
| NY Open | 1.0 pts | ✅ Allowed |
| NY Mid | 1.2 pts | ⚠️ Reduced |
| Off | — | ❌ Blocked |

#### US30
| Session | Max Spread | Permission |
|---------|------------|------------|
| London Open | 2.3 pts | ✅ Allowed |
| NY Open | 2.4 pts | ✅ Allowed |
| NY Mid | 2.8 pts | ❌ Blocked |
| Off | — | ❌ Blocked |

#### XAUUSD (Gold)
| Session | Max Spread | Permission |
|---------|------------|------------|
| London→NY | 0.28 | ✅ Allowed |
| NY Open | 0.30 | ✅ Allowed |
| Asia | 0.32 | ⚠️ Reduced |
| Off | — | ❌ Blocked |

#### FX (EUR/GBP/JPY)
| Session | EUR | GBP | JPY | Permission |
|---------|-----|-----|-----|------------|
| London→NY | 0.18 | 0.32 | 0.24 | ✅ Allowed |
| London only | 0.23 | 0.37 | 0.29 | ⚠️ Reduced |
| Asia (JPY only) | — | — | 0.26 | ⚠️ Reduced |
| Off | — | — | — | ❌ Blocked |

**HARD RULE:** Spread gates are absolute. Never override to "see trades."

### 2️⃣ News Filter Module

**Goal:** Avoid spread blowouts + synthetic repricing around high-impact releases.

#### Events Blocked (High Impact Only)
- NFP, CPI, FOMC (US)
- ECB / BoE rate decisions
- US ISM / Payrolls
- CPI (EU/UK), GDP (major)

#### Timing (Exact)
- Block new entries: **−120s to +120s** around event
- Existing positions: manage exits only (no adds)

#### Symbol Scope
- **Indices:** Block US news for US indices; EU news for GER40/UK100
- **FX:** Block pair-specific currency news
- **Gold:** Block US CPI/FOMC

```cpp
// Pseudocode
if (high_impact_news(symbol, now ± 120s)) {
    block_new_entries();
}
```

**This single rule eliminates the "random loss" days on BlackBull.**

### 3️⃣ Capital & Margin Scaling

**Principle:** Scale only when conditions are best; never via leverage creep.

#### Base Risk
| Parameter | Value |
|-----------|-------|
| Per trade risk | 0.25% – 0.50% equity |
| Max concurrent CFDs | 2 |

#### Session Multipliers (apply to SIZE, not thresholds)
| Session | Multiplier |
|---------|------------|
| Asia | 0.6× |
| Pre-London | 0.8× |
| **London Open** | **1.4×** |
| London→NY | 1.2× |
| **NY Open** | **1.6×** |
| NY Mid | 1.0× |
| Post-NY | 0.7× |

#### Scale-Up (SAFE - Non-Martingale)
| Rule | Value |
|------|-------|
| Min open PnL | +0.5R |
| Max adds | 1 |
| Add size | 50% of initial |
| Stop after add | Break-even |
| Session mult required | ≥1.2× |
| Forbidden regimes | TRANSITION |

#### Drawdown Guards
| Timeframe | Limit | Action |
|-----------|-------|--------|
| Session | 1.0R | Pause session |
| Daily | 2.0R | Trading stops |
| Weekly | 3.5R | Size halves next week |

**NEVER increase leverage to "recover." Scaling is conditional, not emotional.**

### Usage Example

```cpp
#include "config/BlackBullConfig.hpp"
using namespace chimera::cfd::config;

// Full gate check with sizing
auto gate = BlackBullGate::check(
    "NAS100",           // symbol
    0.9,                // current_spread
    MarketRegime::TRENDING,
    dd_state,           // DrawdownState
    10000.0,            // equity
    0.005,              // stop_distance_pct (0.5%)
    1                   // concurrent_positions
);

if (gate.allowed) {
    double size = gate.position_size;
    // Execute trade with calculated size
} else {
    // Blocked: gate.block_reason
}

// Quick check (spread + news only)
if (BlackBullGate::quick_check("XAUUSD", 0.28)) {
    // OK to proceed with sizing
}

// Scale-up check
auto scale = check_scale_up_now(
    "NAS100", current_spread, current_pnl_r,
    regime, adds_so_far, initial_size,
    entry_price, current_price, is_long
);

if (scale.allowed) {
    // Add scale.add_size to position
    // Move stop to scale.new_stop_price
}
```

### Hard Rules That Keep This Profitable

❌ Never trade outside preferred sessions  
❌ Never override spread thresholds  
❌ Never add to losers  
❌ Never add in TRANSITION regime  
❌ Never relax news blocks  
❌ Never increase leverage to recover
