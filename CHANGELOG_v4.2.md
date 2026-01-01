# Chimera v4.2 - AllowTradeHFT Gate + Bootstrap Relaxation

## v4.2.2 - Raw Edge Gating Fix (CRITICAL) - 2024-12-26

### PART A: Raw Edge Gating Fix
Trade existence was decided on SCALED edge instead of RAW edge.
Confidence, regime, vol-cap were crushing small-but-real edges BEFORE gating.

### GOLDEN RULE (NOW ENFORCED)
```
Trade EXISTENCE → decided on RAW edge
Trade SIZE      → decided on SCALED edge
```

### PART B: Data Flow / Bootstrap Fixes

**ROOT CAUSE:** Warmup and level requirements too strict - data never flows to trading logic.

| # | Fix | Engine | Before | After |
|---|-----|--------|--------|-------|
| 1 | book_ok levels | Crypto | >= 10 | >= 5 |
| 2 | Shadow warmup | Crypto | 100 ticks | 20 ticks |
| 3 | Strategy warmup | CFD | 50 ticks | 10 ticks |
| 4 | Realized vol samples | CFD | 50 samples | 10 samples |
| 5 | MicroState warmup | CFD | 30 ticks | 10 ticks |

### PART C: Win Rate Improvements (6 Fixes)

**Expected Impact:** Win rate ↑ 15-25 percentage points, trade count ↓ 20-40%

| # | Fix | Engine | Description | Win Rate Gain |
|---|-----|--------|-------------|---------------|
| 1 | Edge Confirmation | Both | 200µs delay before entry (filters flicker/spoofing) | +8-15% |
| 2 | Ranging Hard Kill | Both | Block trades when ranging + low displacement | +5-10% |
| 3 | Slow Bleed Exit | Both | Exit after 800ms if PnL < 0.5bps (turns losers to scratches) | +3-5% |
| 4 | Asymmetric Exits | Both | Fast TP at 60% of target (lock small wins quickly) | +3-5% |
| 5 | Directional Bias | Both | Don't fight micro-trend (1s EMA direction filter) | +5-8% |
| 6 | Symbol Self-Healing | Both | Disable symbol for day if WR < 40% after 5 trades | +2-5% |

#### New Files Created
- `include/risk/SymbolHealth.hpp` - Auto-disable unhealthy symbols
- `include/microstructure/EdgeController.hpp` - Dynamic edge weighting
- `include/metrics/TradeOpportunityMetrics.hpp` - "Why are we not trading?" visibility

#### Implementation Details

**1. Edge Confirmation (200µs delay)**
```cpp
if (edge_confirm_start_ns == 0)
    edge_confirm_start_ns = now_ns;
if (now_ns - edge_confirm_start_ns < 200'000)
    return;  // Wait for edge persistence
```

**2. Ranging Hard Kill**
```cpp
if (is_ranging && displacement_bps < spread_bps * 2.5)
    return;  // No trades in ranging chop
```

**3. Slow Bleed Exit**
```cpp
if (hold_ms > 800 && pnl_bps < 0.5 && pnl_bps > -sl_bps * 0.5)
    exit_trade("SLOW_BLEED");  // Scratch before it bleeds more
```

**4. Asymmetric Exits**
```cpp
double fast_tp = tp_bps * 0.6;  // Lock small wins at 60%
if (pnl_bps >= fast_tp)
    exit_trade("TP_FAST");
```

**5. Directional Bias Filter**
```cpp
micro_trend_ema = 0.05 * price_delta + 0.95 * micro_trend_ema;
if (micro_trend > 0 && direction < 0) return;  // Don't fight trend
if (micro_trend < 0 && direction > 0) return;
```

**6. Symbol Self-Healing**
```cpp
if (trades_today >= 5 && rolling_winrate() < 0.40)
    disabled_for_day = true;
```

### FIXES APPLIED (Raw Edge)

| # | Fix | Engine | Description |
|---|-----|--------|-------------|
| 1 | Raw edge in compute_projected_edge | Crypto | Removed vol cap from return value |
| 2 | Raw edge in AllowTradeHFT | Crypto | Gate on raw_edge_bps, not capped edge |
| 3 | Raw edge in AllowTradeHFT | CFD | Gate on raw_edge_bps, remove pre-gate vol cap |
| 4 | Vol cap post-gate only | Both | Sizing uses vol-capped, existence uses raw |

### WHAT WAS BROKEN (v4.2.0-v4.2.1)
```cpp
// WRONG (v4.2.1)
double raw_edge = compute_edge();       // Already vol-capped!
double projected_edge = min(raw_edge, vol_cap);  // Double-capped!
if (projected_edge < min_edge) return LOW_EDGE;  // Crushed valid edges

// CORRECT (v4.2.2)
double raw_edge_bps = compute_edge();   // No cap
if (raw_edge_bps < min_edge) return LOW_EDGE;  // Gate on raw
// Vol cap only affects sizing AFTER gate passes
```

### EXPECTED BEHAVIOR
- **Within 30-90 seconds:** SOL trades first, BTC/ETH occasionally
- **Trade count:** Lower than before (by design - filtering noise)
- **Win rate:** 55-70% expected (up from 28%)
- **Exit reasons:** TP_FAST (most common), TP, SLOW_BLEED, SL
- **CFD:** Trades during London session, mostly quiet Asia
- **LOW_EDGE blocks:** Still expected (good - gate is working)
- **Bootstrap counter:** Actually advances now

---

## v4.2.1 - Bootstrap Relaxation & Crypto Microstructure (2024-12-26)

### ROOT CAUSE ANALYSIS
The system showed `[BLOCK] LOW_EDGE` and `NEG_EXPECTANCY` with zero trades because:
1. ExpectancyAuthority blocked before any trades could form expectancy (deadlock)
2. Vol cap killed edge in calm markets (realized_vol < 1 bps → edge zeroed)
3. Displacement rule too strict (required 2× spread, crypto often moves < 1 bp)
4. Crypto microstructure edge (imbalance) not contributing to edge calculation

### CRITICAL FIXES

| # | Fix | Engine | Change |
|---|-----|--------|--------|
| 1 | Bootstrap bypass | Crypto | `if (trades < 20) return FULL_SIZE` |
| 2 | Bootstrap edge | Crypto | `mult=1.2, min=1.0` during bootstrap |
| 3 | Bootstrap edge | CFD | `mult=1.4, min=1.5` during bootstrap (15 trades) |
| 4 | Vol cap floor | Both | `max(vol * 0.8, 1.5)` prevents permanent LOW_EDGE |
| 5 | Imbalance boost | Crypto | `edge += abs(imbalance) * 2.0` |
| 6 | Imbalance override | Crypto | Strong imbalance bypasses chop check |
| 7 | Adaptive chop | Both | `max(spread * 1.2, 1.0)` instead of `spread * 2.0` |
| 8 | Symbol normalize | CFD | Handles XAUUSD., NAS100.cash |
| 9 | Remove double vol | CFD | Removed vol_ratio confidence penalty |

### EXPECTED BEHAVIOR
**Within 1-3 minutes:** 1-5 trades, many `[BLOCK] LOW_EDGE` (good), bootstrap advancing
**After bootstrap:** Full strict rules re-engage, win rate 45-60%

---

## v4.2.0 - AllowTradeHFT Gate (2024-12-26)

**Core Problem:** Win rate collapsed to ~28% because trades fired where edge < cost.

**The Fix:** `AllowTradeHFT` gate - If edge < cost × safety → trade must not exist.

### Gate Checks
1. Spread sanity
2. Total cost calculation
3. Imbalance boost (crypto only)
4. Absolute edge floor
5. **HARD EDGE VS COST** (THE INVARIANT)
6. Volatility cap with floor
7. Chop kill switch (displacement + imbalance)
8. Loss cooldown
9. Frequency limit

### Asset Class Profiles (CFD)

| Profile | Spread | Edge (prod) | Mult (prod) | TP/SL |
|---------|--------|-------------|-------------|-------|
| FX Major | 1.5bps | 4.0bps | 2.8× | 10/-4 |
| Metals | 2.5bps | 6.0bps | 3.0× | 18/-6 |
| Indices | 2.0bps | 5.0bps | 2.5× | 15/-5 |

---

## Deploy

```bash
cp /mnt/c/Chimera/Chimera_v4.2.2.zip ~ && cd ~ && rm -rf Chimera && unzip Chimera_v4.2.2.zip && mv Chimera_v4.2.2 Chimera && cd Chimera && mkdir -p build && cd build && cmake .. && make -j$(nproc) && ./chimera
```

## Rules (Non-Negotiable)
1. If raw_edge < cost × safety → trade must not exist
2. Bootstrap allows seeding, but still requires raw_edge > cost
3. After bootstrap, full strict rules re-engage automatically
4. Vol cap affects SIZING only, not EXISTENCE
