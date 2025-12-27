# Crypto Burst Engine - Opportunistic Trading Module

## Status: v1.0.0 - LOCKED DESIGN

## Overview

The Crypto Burst Engine implements an **opportunistic burst trading strategy** for crypto assets. This is fundamentally different from the existing market-making scalping engine (`CryptoScalpEngine`).

### Key Differences

| Aspect | Scalping Engine | Burst Engine |
|--------|-----------------|--------------|
| Strategy | Market making, continuous quoting | Burst momentum capture |
| Trade Frequency | High (dozens/day) | Very Low (0-3/week) |
| Default State | ON, looking for opportunities | OFF, waiting for conditions |
| Entry Type | Maker (passive) | Taker (aggressive) |
| Hold Time | Milliseconds to seconds | 5-30 seconds |
| Position Sizing | Fixed small | Single entry, no scaling |

## Operating Contract (CRITICAL)

**READ THIS BEFORE DEPLOYING:**

1. **Crypto is OFF by default.** It turns ON only when ALL pre-gate conditions are simultaneously met. There is no "tuning to see more trades."

2. **Success metric:** RARE, high-expectancy wins; ZERO bleed otherwise. If crypto trades frequently → something is WRONG.

3. **Expected behavior:**
   - Days with 0 trades: NORMAL and CORRECT
   - 1-3 trades per week: OPTIMAL
   - >5 trades per week: INVESTIGATE - gate likely compromised

4. **Silence is intentional.** When idle, the engine logs WHY it's idle. "No trade" = system protecting capital, NOT failing.

5. **NEVER:**
   - Relax pre-gate conditions to "see action"
   - Add symbols beyond the approved list
   - Re-enter during cooldown
   - Scale into positions

## Pre-Gate Conditions (ALL Must Pass)

```
Market Structure Requirements:
├── Volatility: realized vol ≥ 2.0× trailing 30-min median
├── Spread: spread ≤ p25 of last 30 minutes (compressed, not widening)
├── Imbalance: top-10 book imbalance ≥ 65/35
├── Displacement: price move ≥ 6 ticks
├── Regime: confirmed TRENDING (not TRANSITION or RANGING)
└── Edge: edge ≥ 3× total cost (spread + round-trip fees)

Risk Requirements:
├── No existing position
├── Not in cooldown
├── Daily loss limit not hit
└── Max daily trades not exceeded
```

**If ANY condition fails → engine stays idle**

## Entry Rules

- One direction only (following imbalance)
- One entry (no scaling-in, EVER)
- Taker execution (fees irrelevant vs burst move)
- WebSocket API only (not REST - preserves latency edge)

## Exit Rules

- **Time stop:** 5-30 seconds (hard limits)
- **Structure break:** Imbalance collapses to ~50/50
- **Max adverse:** 0.5R excursion (tight)
- **Cooldown after exit:** 5 minutes (win) / 15 minutes (loss)

## Symbols

| Symbol | Mode | Status |
|--------|------|--------|
| BTCUSDT | LIVE | Primary - production enabled |
| ETHUSDT | SHADOW | Paper trading only |
| SOLUSDT | SHADOW | Paper trading only |

**NEVER add long-tail alts**

## Files

```
crypto_engine/include/burst/
├── CryptoBurstEngine.hpp      # Main engine implementation
└── BurstBinanceAdapter.hpp    # Binance WebSocket integration
```

## Integration Example

```cpp
#include "burst/CryptoBurstEngine.hpp"
#include "burst/BurstBinanceAdapter.hpp"

// Create engine with BTC-only config
auto burst_adapter = chimera::crypto::burst::create_btc_burst_adapter(
    order_sender  // nullptr for shadow mode
);

// Start engine
burst_adapter->engine()->start();

// Feed from Binance WebSocket (in your existing callbacks):
void on_binance_depth(const std::string& symbol, ...) {
    burst_adapter->on_depth_update(symbol, bids, asks, exchange_ts);
}

void on_binance_agg_trade(const std::string& symbol, ...) {
    burst_adapter->on_agg_trade(symbol, price, qty, is_buyer_maker, ts);
}
```

## Observability

When idle, the engine logs a single line every 60 seconds:

```
[CRYPTO] OFF — vol=1.3x(LOW) spread=p45(WIDE) imbal=52/48(WEAK) disp=3t(LOW) regime=RANGING(BLOCKED) cd=0s
```

This prevents second-guessing. Every idle state is explained.

When conditions align:

```
[CRYPTO-BURST] SIGNAL: BTCUSDT LONG vol=2.3x imbal=68/32 disp=8t edge=15.2bps
[CRYPTO-BURST] ENTRY: BTCUSDT LONG @ 98542.00 size=0.000500
[CRYPTO-BURST] EXIT: BTCUSDT @ 98567.00 PnL=$0.12 (0.48R) reason=TIME_STOP hold=12340ms
[CRYPTO-BURST] Cooldown: BTCUSDT for 300s (win)
```

## Configuration

### Gate Thresholds (BurstGateConfig)

```cpp
struct BurstGateConfig {
    double vol_expansion_min = 2.0;       // >= 2x trailing median
    double spread_percentile_max = 25.0;  // <= p25
    double imbalance_ratio_min = 0.65;    // >= 65/35
    int displacement_ticks_min = 6;       // >= 6 ticks
    Regime required_regime = TRENDING;    // Must be TRENDING
    double edge_to_cost_min = 3.0;        // >= 3x cost
};
```

### Exit Configuration (BurstExitConfig)

```cpp
struct BurstExitConfig {
    int time_stop_min_sec = 5;            // Minimum hold
    int time_stop_max_sec = 30;           // Maximum hold
    double max_adverse_r = 0.5;           // Max adverse excursion
    bool structure_break_exit = true;     // Exit on imbalance collapse
    double imbalance_collapse_threshold = 0.50;  // 50/50 = collapsed
};
```

### Cooldown Configuration (BurstCooldownConfig)

```cpp
struct BurstCooldownConfig {
    int cooldown_after_win_sec = 300;     // 5 minutes after win
    int cooldown_after_loss_sec = 900;    // 15 minutes after loss
    int cooldown_after_no_fill_sec = 60;  // 1 minute after no fill
};
```

### Daily Limits

```cpp
double daily_loss_limit_usd = 100.0;      // Hard stop for day
int max_daily_trades = 5;                 // Circuit breaker
```

## What "Done" Looks Like

You are operating correctly when:

- CFDs produce steady, boring equity growth (primary engine)
- Crypto trades are rare and meaningful (burst engine)
- Weeks pass with no config changes
- Logs explain why trades didn't happen
- You stop asking "why didn't it trade?"

**Most systems never reach this clarity.**

## Hard Rules That Keep This Profitable

These override everything:

❌ Never trade RANGING regimes  
❌ Never relax spread thresholds  
❌ Never disable cooldown  
❌ Never add to positions  
❌ Never scale into losers  
❌ Never override daily limits  

## Version History

- **v1.0.0** - Initial implementation
  - BTC LIVE, ETH/SOL SHADOW
  - Full pre-gate implementation
  - Time/structure/adverse exits
  - Hard cooldown enforcement
  - Binance WebSocket integration
