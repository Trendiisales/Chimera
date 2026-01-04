# Income Engine Specification v1.0

## 🔒 LOCKED RULES (DO NOT MODIFY)

### ML Rules
- **ML cannot trigger** - only entry logic triggers
- **ML cannot size** - sizing is fixed
- **ML cannot override risk** - risk caps are hard
- **ML only vetoes** - that's the entire job
- **If ML fails → veto all** - safe default

### Symbol Rules
- **NAS100 ONLY** - no exceptions
- **XAUUSD HARD DISABLED** - Gold tempts on quiet days, that temptation is lethal
- **No symbol additions** without 20+ sessions proof

### Threshold Rules
- **ML threshold = 0.60** - FIXED, not dynamic
- **No adaptive learning** - no online retraining
- **No dynamic thresholds** - they drift and kill

---

## 📊 ML VETO VISIBILITY (MANDATORY)

### Why This Matters
If ML is invisible, you will distrust it. If you distrust it, you will weaken it. That's how income engines die.

### Engine-Level Veto Log
Every time ML blocks a trade, log this explicitly:
```
[INCOME][ML-VETO]
  symbol=NAS100
  score=0.47
  threshold=0.60
  reason=REGIME_UNSUITABLE
  features={vol_pct=82, compression=0.91, spread_unstable=TRUE, impulse=HIGH}
  timestamp=2025-01-xxT09:41:12Z
```

**Rules:**
- One line per veto
- No aggregation
- No suppression
- No "only log on debug"

### Dashboard Requirements (income_dashboard.html)

**Panel A — ML Regime Score (Time Series)**
- X-axis: time
- Y-axis: P(regime_suitable)
- Horizontal line at 0.60
- Shade regions where score < threshold
- Visual output: "dead days", "good windows", "regime shifts"

**Panel B — Veto Counters**
Per session:
- ML veto count
- Spread veto count
- Liquidity veto count
- Session veto count

Health check:
- If ML vetoes nothing → it's broken
- If ML vetoes everything → thresholds wrong

**Panel C — "Why No Trade?" (Single Line)**
When no trade fires, GUI shows:
```
IncomeEngine idle: ML veto (score 0.52)
```
Not silence. Silence breeds bad decisions.

---

## 💰 PnL ATTRIBUTION (SEPARATE BUCKET)

### Mandatory Separation
IncomeEngine must write to its own PnL stream:
```
PnLBucket::IncomeEngine
```

**Never:**
- Mixed with Alpha
- Netted with portfolio
- Hidden inside execution logs

### Per-Trade Metrics (MANDATORY)
- MAE (max adverse excursion)
- MFE (max favorable excursion)
- Time in trade (ticks)
- Exit reason (TP / SL / TRAIL / TIME / VETO_EXIT)

### Per-Session Metrics (MANDATORY)
- Trades fired
- Trades vetoed
- Scratch rate
- Net PnL
- Max drawdown

### What You DO NOT Optimize
- Win rate
- Average win
- R:R ratio

Those are Alpha metrics, not income metrics.

### Healthy PnL Profile (After 10 Sessions)
- Many 0-trade days ✓
- Many ±0.05% days ✓
- Very few >0.15% days ✓
- No scary days ✓

**If you see excitement, it's wrong.**

---

## 🎯 SUCCESS/FAILURE CRITERIA (10 LONDON SESSIONS)

### AUTOMATIC FAIL CONDITIONS (Stop Immediately)
If any occur → engine is not ready:

❌ Daily drawdown > -0.50%
❌ >10 trades in single session
❌ Trades during:
  - London open (08:00 UTC)
  - NY open (13:30-14:30 UTC)
  - Asia session
❌ ML veto overridden manually
❌ Income trades overlap Alpha trades

**One violation = stop and review. No exceptions.**

### PASS CONDITIONS (All Required)
After 10 London sessions, all must be true:

✅ Max daily DD ≤ -0.50%
✅ Median trades/day ≤ 4
✅ At least 30-50% of sessions have 0 trades
✅ ML vetoes occur regularly
✅ IncomeEngine PnL variance < Alpha variance
✅ No correlation spikes with Alpha

**Profit does not need to be impressive yet.**

### STRONG PASS (Green Light for Live Micro)
Bonus criteria, not required:
- ≥60% green sessions
- Average DD < -0.20%
- MAE consistently < stop distance
- ML veto days coincide with ugly market days

**If you see this → you're building something real.**

---

## 🔬 ML FEATURE VECTOR

### ML's Sole Question
> "Is this market environment suitable for behavior-based income trading right now?"

That's it. Not prediction. Not direction. Environment quality.

### Feature Set (All Backward-Looking, Stationary, Normalized)

**Volatility & Range**
```
realized_vol_1m
realized_vol_5m
vol_percentile_session
range_5m / range_session_avg
compression_ratio
```

**Microstructure Stability**
```
spread_mean_1m
spread_std_1m
spread_widen_events
depth_bid_mean
depth_ask_mean
depth_imbalance
```

**Price Behavior**
```
impulse_count_5m
impulse_decay_time
mean_reversion_speed
```

**Context**
```
time_of_day_bucket   (one-hot)
session_flag         (London_mid / NY_mid)
crypto_stress_flag   (binary)
```

### 🚫 Explicitly Excluded
- Direction
- Returns
- PnL
- Future bars
- Alpha signals

### Labels (Training)
You do not label direction. You label environment quality.
```
label = 1 if:
    compression detected
    AND income-style trade would not hit SL
    AND MAE < stop_distance
    AND no impulse breakout followed

label = 0 otherwise
```

### Model Choice
Use one of:
- Logistic Regression (default)
- Gradient Boosted Trees (shallow, max 50 trees)

Target output: `P(regime_suitable)`

Inference rule (LOCKED):
```
if P < 0.60 → veto
```

### Failure-Safe Rule (Critical)
If:
- ML is offline
- Model returns NaN
- Feature vector incomplete

→ **IncomeEngine is vetoed entirely**

Safe default = don't trade.

---

## 📋 SHADOW-MODE REVIEW PROTOCOL

### What You DO NOT Look At
- Individual trade PnL
- Win rate
- "It feels quiet"
- "It feels good/bad"

**Ignore all of that.**

### What You MUST Look At (Daily)

**Engine Activity**
```
Trades fired
Trades vetoed
Idle windows
```
Healthy: 0-4 trades, many vetoes, long idle periods

**Risk Shape**
```
Max adverse excursion
Time in market
Daily drawdown
```
Healthy: MAE small and consistent, time-in-market low, no sharp DD spikes

**ML Behavior**
```
Average regime score
Score volatility
Veto clustering
```
Healthy: score moves slowly, vetoes cluster on ugly days, good days show clear "green windows"

### Red Flags (Immediate Stop)
❌ >6 trades in one session
❌ Trades during impulse expansions
❌ ML never vetoes
❌ ML vetoes everything
❌ DD > -0.50%

**Any one = stop, diagnose.**

### What a GOOD London Session Looks Like
- Possibly no trades
- Or 1-3 trades
- Small scratches
- No excitement
- Nothing scary

**Boring = correct.**

---

## 🚪 EURUSD PHASE-2 GATE (Not Active)

You do not add EURUSD now. This is the exact criteria for later.

### Preconditions (ALL Required)
- ≥20 London sessions logged
- IncomeEngine DD never > -0.5%
- NAS100 income variance < Alpha variance
- ML veto accuracy stable
- Zero Alpha/Income overlap issues

**Miss one → EURUSD stays disabled.**

### EURUSD Must Pass These Checks

**Structural**
- Spread ≤ 0.8 pip consistently
- No synthetic widening
- Good depth during London

**Behavioral**
- Clear compression windows
- Mean reversion > impulse continuation
- Predictable session rhythm

**ML Compatibility**
- Feature distributions overlap NAS100
- Regime scores meaningful
- No constant veto / constant approval

**If ML can't distinguish regimes → EURUSD rejected.**

### EURUSD Income Rules (If Ever Enabled)
Much tighter than NAS100:
```
stop       = 4-6 pips
target     = 3-5 pips
timeout    = 60s
risk/trade = HALF of NAS100
```

And:
- London only
- No NY initially
- No news adjacency

---

## ✅ PRE-LONDON CHECKLIST

Before you let this run in demo:

### Engine Structure
- [ ] IncomeEngine isolated
- [ ] NAS100 only
- [ ] XAUUSD hard-disabled
- [ ] No shared state with Alpha

### Risk
- [ ] Daily loss ladder enforced
- [ ] Trade cap enforced (6/day, 10 = hard fail)
- [ ] No pyramids
- [ ] No scaling

### ML
- [ ] Regime score visible
- [ ] Veto reasons logged (every single one)
- [ ] Threshold fixed at 0.60
- [ ] ML failure = veto all (safe default)

### Metrics
- [ ] Separate PnL bucket
- [ ] MAE/MFE logged per trade
- [ ] Exit reasons logged
- [ ] Session stats tracked

**If any box is unchecked → do not run.**

---

## 📝 Session Log Template

After each London session, fill this out:

```
Session: YYYY-MM-DD London
Trades Fired: X
Trades Vetoed: X
ML Vetoes: X
Net PnL: X.XX bps
Max DD: X.XX bps
Avg MAE: X.XX bps
Avg MFE: X.XX bps

Exit Breakdown:
  TP: X | SL: X | TRAIL: X | TIME: X

Notes:
_________________________________

Pass/Fail:
[ ] DD ≤ -0.50%
[ ] Trades ≤ 10
[ ] No open trades
[ ] ML not overridden
[ ] No Alpha overlap
```

---

## 🎯 The Uncomfortable Truth

You are building something most traders never manage to build:
- A system that can pay steadily
- Without chasing
- Without scaling recklessly
- Without pretending prediction is possible

That's why this feels uncomfortable.

**If it feels boring, constrained, and slightly underwhelming — you're probably doing it right.**
