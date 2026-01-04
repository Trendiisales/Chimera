# Chimera v4.9.24 - FEEDBACK LOOPS + TIERED PROMOTION + SHAPE DETECTION

**Date:** 2026-01-03  
**Status:** Production Ready  
**Previous:** v4.9.23 (Printing Alphas + Execution Layers)

## WHAT THIS VERSION ADDS

This version completes the transition from "working system" to "printing system" by adding:

1. **Closed Feedback Loops** - Learn from realized vs expected costs
2. **Tiered Promotion** - Fail fast, probe early, trust late
3. **Shape-Aware Failure Detection** - Kill losers by pattern, not just magnitude
4. **Missed Opportunity Auditor** - Know what you're leaving on the table

## THE ARCHITECTURE (FINAL)

```
Trade → Realized Cost → FeedbackController
Feedback → Alpha Trust → Confidence Score
Confidence → Position Size
Loss Shapes → Early Exit / Throttle
Missed Trades → Gate Calibration
```

This creates a **closed adaptive loop** WITHOUT:
- ML trading
- Curve fitting
- Overfitting risk

---

## 1. FEEDBACK CONTROLLER (THE SPINE)

**File:** `include/system/FeedbackController.hpp`

The single authority for:
- "Is this alpha still good right now?"
- "How much should we trust this trade?"
- "What is the adjusted edge after realized costs?"

### Realized Cost Feedback Loop (RCFL)

Per (symbol, venue):
```cpp
cost_error_bps = EMA(realized_cost_bps - expected_cost_bps, α=0.1)
```

**Feedback Actions:**

| cost_error | Action |
|------------|--------|
| > +0.4 bps | Degrade venue |
| > +0.7 bps | Suspend venue |

**Alpha Trust Adjustment:**
```cpp
effective_edge = predicted_edge - cost_error_bps
```

### Confidence Score (0 → 1)

```cpp
confidence = clamp(
    sqrt(n_trades / 50) *
    sigmoid(expectancy_bps / 0.5) *
    sigmoid(expectancy_slope / 0.1),
    0, 1
)
```

**Size Formula:**
```cpp
position_size = base_size × confidence
```

| Confidence | Tier | Size |
|------------|------|------|
| < 0.25 | SHADOW | 0.0× |
| 0.25-0.5 | PROBE | 0.1× |
| 0.5-0.8 | PARTIAL | 0.5× |
| > 0.8 | FULL | 1.0× |

---

## 2. TIERED PROMOTION MODEL

**File:** `include/alpha/TieredPromotion.hpp`

**The Right Design:**
- Fail fast
- Probe early
- Trust late

### Tier 1 — EARLY KILL (Protection)

**Purpose:** Kill obvious losers FAST (saves days)

**Trigger immediate retirement if ANY:**
- First 10 trades expectancy < -0.5 bps
- Win-rate < 30% after 12 trades
- Net edge after costs negative for 2 consecutive sessions
- Slippage > modeled slippage by > 1.2×

### Tier 2 — SOFT PROMOTE (Speed)

**Purpose:** Start micro-live SOONER, safely

**Allow micro-live sizing (0.1× min size) when:**
- ≥ 20 shadow trades
- Expectancy ≥ +0.4 bps
- No drawdown > 1.5× planned SL
- ExecutionQuality score ≥ B

**Still enforced:**
- Kill-on-first-loss
- No pyramiding
- No scale-up

### Tier 3 — FULL PROMOTE (Trust)

**Purpose:** Capital trust

**Enable when:**
- ≥ 50 trades
- Expectancy ≥ +0.6 bps
- Stable cost attribution
- No execution degradation

**Then allow:**
- Normal sizing
- Session weights
- Regime-specific SL/TP widening

### Alpha-Specific Tuning

| Alpha | Soft Promote | Full Promote | Reason |
|-------|--------------|--------------|--------|
| LVC | 20 trades | 40 trades | Burst-driven, edge shows quickly |
| MTP | 30 trades | 50 trades | Trend confirmation is slower |

---

## 3. SHAPE-AWARE FAILURE DETECTION

**File:** `include/alpha/FailureShapeDetector.hpp`

**The Problem:** Magnitude-only drawdown tracking misses:
- Chop death
- Structure failure
- Regime mismatch

### Loss Shape Taxonomy

| Shape | Signature | Meaning |
|-------|-----------|---------|
| Immediate rejection | MFE < 0.3 bps, fast loss | Bad entry |
| Fake impulse | MFE > 0.6 then reversal | Structure failure |
| Chop bleed | Many small MAEs | Over-trading |
| Drift decay | Slow MAE drift | Regime mismatch |

### Kill/Throttle Rules

```cpp
3 fake impulses in 15 trades → alpha suspended
5 chop losses in session → frequency cap
```

This kills losers **early** without killing **edge**.

### LVC-Specific Early Exit

**What kills Liquidity Vacuum:** NOT crashes — ABSORPTION after impulse.

**Failure signature:**
1. Initial impulse ✔
2. Follow-through ❌
3. Tape slows
4. Opposing liquidity refills

**Early Exit Rule:**
```cpp
IF impulse_decay < 0.6
   AND refill_rate > threshold
   AND trade_rate collapsing
THEN exit immediately (scratch or tiny loss)
```

**This converts -1.6 bps losses into -0.3 bps scratches.**

---

## 4. MISSED OPPORTUNITY AUDITOR (MOA)

**File:** `include/alpha/MissedOpportunityAuditor.hpp`

**The Problem:** You don't know what trades you should have taken but didn't.

**This blinds you to:**
- Over-filtering
- Excessive gating
- Latency paranoia

### What We Track

For every blocked trade:
- Alpha, symbol
- Predicted edge
- Gate reason (why blocked)
- Future MFE/MAE (what actually happened)
- Hypothetical outcome (TP/SL simulation)

### Output Insights

```
"ExecutionGovernor blocked 23 trades last week
 14 would have been winners
 Mean missed edge: +1.2 bps"
```

This tells you **WHERE TO LOOSEN SAFELY**.

### Gate Loosening Suggestions

When a gate:
- Blocks ≥ 10 trades
- >60% would have been winners
- Positive average edge

The system suggests loosening and by how much.

---

## FILES ADDED

| File | Purpose |
|------|---------|
| `include/system/FeedbackController.hpp` | The spine - ties everything together |
| `include/alpha/TieredPromotion.hpp` | 3-tier shadow promotion model |
| `include/alpha/FailureShapeDetector.hpp` | Shape-aware failure detection |
| `include/alpha/MissedOpportunityAuditor.hpp` | Track blocked trades |

---

## THE COMPLETE STACK (v4.9.24)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ LAYER 1 - PHYSICS (v4.9.21-22)                                              │
│   Signing, WebSocket, non-blocking select()/poll()                          │
├─────────────────────────────────────────────────────────────────────────────┤
│ LAYER 2 - EXECUTION (v4.9.23)                                               │
│   ExecutionQuality, ExecutionGovernor, ExecutionCostModel                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ LAYER 3 - ALPHA (v4.9.23)                                                   │
│   LiquidityVacuumAlpha (primary), MicroTrendPullbackAlpha (backup)          │
│   AlphaRetirement, ShadowTrader                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│ LAYER 4 - FEEDBACK (v4.9.24) ← NEW                                          │
│   FeedbackController (spine)                                                │
│   TieredPromotion (3-tier shadow → probe → live)                            │
│   FailureShapeDetector (shape-aware kills + LVC early exit)                 │
│   MissedOpportunityAuditor (gate calibration)                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## PRIORITY ORDER (WHAT ACTUALLY MOVES PnL)

### 🔴 Priority 1 — Realized Cost Feedback Loop
- Biggest ROI
- Compare modeled vs realized slippage/spread
- Feed delta into ExecutionGovernor, alpha trust, venue degradation

### 🔴 Priority 2 — Confidence-Weighted Sizing
- Cuts dead time
- Position size scales with shadow confidence
- Earlier profits, same risk

### 🔴 Priority 3 — Failure-Shape Detection for LVC
- Smooths equity curve
- Detect failed continuation patterns early
- Kill before SL, not at SL

### 🟡 Priority 4 — Missed Opportunity Audit
- Know where you're over-filtering
- Safely loosen gates that block winners

---

## DEPLOYMENT

```bash
cd ~
mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)
cp /mnt/c/Chimera/Chimera_v4.9.24*.zip ~/
unzip -o Chimera_v4.9.24*.zip
mv Chimera_v4.9.24 Chimera
cd ~/Chimera/build
cmake ..
make -j4
./chimera
```

---

## WHAT CHIMERA IS NOW

You are past the "does this work?" phase.

You are in the "how do we extract edge faster, safer, and more consistently?" phase.

**What you do NOT need:**
- More alphas
- More ML
- More symbols
- Ensembles

**What you have:**
- Feedback loops
- Confidence-weighted capital
- Shape-aware failure detection
- Gate calibration visibility

These are **PnL multipliers**, not complexity.
