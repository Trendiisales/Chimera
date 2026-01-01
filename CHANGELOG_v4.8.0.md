# CHIMERA v4.8.0 CHANGELOG

**Release Date:** 2025-01-01  
**Codename:** FAIL-CLOSED  
**Focus:** Critical safety fixes + Why-not-trading diagnostics

---

## 🚨 CRITICAL SAFETY FIX

### ML Fail-Closed Behavior (MLInference.hpp)

**PROBLEM (v4.7.0 and earlier):**
- ML inference errors returned `allow_trade = true` (DANGEROUS)
- Missing ONNX runtime returned `ml_active = true, allow_trade = true` (DANGEROUS)
- Uninitialized state returned `allow_trade = true` (DANGEROUS)

**SOLUTION (v4.8.0):**
- Added `ExecutionMode` enum: `LIVE`, `BACKTEST`, `PAPER`
- LIVE mode: ALL ML errors now return `allow_trade = false, size_multiplier = 0`
- BACKTEST mode: Unchanged (fail-open for backtesting)
- Added `fail_closed_count_` tracking for monitoring

**Impact:** 
- ML can NEVER silently green-light trades in production on error
- System becomes MORE conservative when ML fails, not reckless

---

## 🔧 REGIME STABILITY FIX

### Removed Placeholder (IntentEnforcer.hpp)

**PROBLEM (v4.7.0):**
```cpp
true,  // regime_stable placeholder  <-- HARDCODED!
```
- Regime stability was always `true` regardless of actual conditions
- Diagnostics showed fake regime state
- Couldn't tune based on real regime data

**SOLUTION (v4.8.0):**
- Added `RegimeStabilityTracker` class with per-symbol tracking
- `updateIntent()` now stores actual regime stability
- `isRegimeStable()` query returns real value
- Replay logs now contain accurate regime data

**Impact:**
- Regime now actually controls trade admission
- Diagnostics reflect reality
- Proper tuning possible

---

## 🔧 CRITICAL BUILD FIX: Enum Unification

### Multiple Enum Definition Conflicts

**PROBLEM (v4.7.0):**
- `BlockReason` defined in both TradeOpportunityMetrics.hpp AND IntentGate.hpp (different values!)
- `LatencyState` defined in both ExecutionAuthority.hpp AND ScalpProfile.hpp
- Build failed with "multiple definition" errors

**SOLUTION (v4.8.0):**
Created `include/shared/ChimeraEnums.hpp` as single source of truth:
- All shared enums centralized in one file
- Removed duplicate definitions from all other files
- Fixed `IntentGateConfig` struct to compile with default values
- Added `getSymbolIntent()` method to IntentGate class

**Files Modified:**
- `include/shared/ChimeraEnums.hpp` (NEW) - Unified enum definitions
- `include/shared/IntentGate.hpp` - Uses ChimeraEnums.hpp
- `include/core/ExecutionAuthority.hpp` - Uses ChimeraEnums.hpp  
- `include/core/ScalpProfile.hpp` - Uses ChimeraEnums.hpp
- `include/metrics/TradeOpportunityMetrics.hpp` - Uses ChimeraEnums.hpp

**Impact:**
- Clean compilation without enum conflicts
- Single source of truth for shared types

---

## 📊 NEW: Why-Not-Trading Panel

### TradeDecisionState.hpp (NEW FILE)

Real-time per-symbol diagnostics answering: **"WHY didn't X trade?"**

**Fields tracked:**
- `symbol`, `profile`, `session`
- `allowed`, `veto_reason`
- `edge`, `edge_threshold`
- `persistence`, `persistence_threshold`
- `spread_bps`, `spread_threshold`
- `imbalance`, `imbalance_threshold`
- `range_expansion`, `range_threshold`
- `regime`, `regime_stable`
- `latency_state`, `latency_ms`
- `shock_active`, `shock_cooldown_sec`
- `structure_state` (for INDEX_STRUCTURE)
- `absorption_score`, `range_percentile`
- `fix_connected`, `intent_live`

**Features:**
- JSON serialization for WebSocket broadcast
- Console diagnostics with pass/fail indicators
- `DecisionStateManager` tracks all symbols

**Example output:**
```
╔════════════════════════════════════════════════════════════╗
║  WHY-NOT-TRADING: XAUUSD                                   ║
╠════════════════════════════════════════════════════════════╣
║  Profile:  SCALP-NY     Session: NY_OPEN                   ║
║  Status:   ✖ EDGE_TOO_LOW                                  ║
╠════════════════════════════════════════════════════════════╣
║  Edge:        0.42 / 0.60 ✖                                ║
║  Persistence: 0.51 / 0.45 ✔                                ║
║  Latency:     NORMAL ✔                                     ║
║  Shock:       CLEAR ✔                                      ║
╚════════════════════════════════════════════════════════════╝
```

---

## 🗺️ NEW: Activity Router

### ActivityRouter.hpp (NEW FILE)

Single source of truth for symbol routing.

**LIVE TRADING SYMBOLS:**
| Symbol  | Mode | SCALP-NY | SCALP-LDN | CORE | Cooldown |
|---------|------|----------|-----------|------|----------|
| XAUUSD  | LIVE | YES      | YES       | YES  | 180s     |
| NAS100  | LIVE | YES      | no        | YES  | 120s     |
| EURUSD  | LIVE | YES      | YES       | no   | 90s      |
| GBPUSD  | LIVE | YES      | YES       | no   | 90s      |
| USDJPY  | LIVE | no       | YES       | no   | 90s      |

**SHADOW ONLY:**
- US30, SPX500, XAGUSD

**Features:**
- `getRoute(symbol)` - Full routing info
- `selectProfile(symbol, session)` - Profile selection
- `isLiveSymbol()`, `isShadowSymbol()`, `isScalpSymbol()`
- `getShockCooldown(symbol)`
- `printRoutingTable()` - Console diagnostics

---

## 📝 FILES CHANGED

### Modified:
- `include/ml/MLInference.hpp` - FAIL-CLOSED behavior
- `include/shared/IntentEnforcer.hpp` - Regime stability wired

### Added:
- `include/core/TradeDecisionState.hpp` - Why-not-trading panel
- `include/core/ActivityRouter.hpp` - Symbol routing table
- `CHANGELOG_v4.8.0.md` - This file

---

## 🚀 DEPLOYMENT

```bash
# Archive existing
cd ~ && mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)

# Deploy new
cp /mnt/c/Chimera/chimera_v4_8_0_ENUM_FIX.zip ~/
unzip -o chimera_v4_8_0_ENUM_FIX.zip && mv chimera_src Chimera

# Build
cd ~/Chimera && rm -rf build && mkdir build && cd build
cmake .. && make -j4

# Run
./chimera
```

---

## ⚠️ BREAKING CHANGES

None. All changes are backward compatible.

---

## 🎯 EXPECTED POST-PATCH BEHAVIOR

1. **ML fails** → Trades BLOCKED (not allowed through)
2. **Regime unstable** → Correctly reflected in diagnostics
3. **Why-not-trading** → Visible in real-time for each symbol
4. **Symbol routing** → Consistent across all code paths

---

## 📋 VERIFICATION CHECKLIST

- [ ] ML fail-closed works: Stop ONNX → verify trades blocked
- [ ] Regime tracking works: Set regime TOXIC → verify logged correctly
- [ ] Decision state works: Check WebSocket output for all symbols
- [ ] Routing table correct: Print table, verify symbols match spec

---

## 🔮 NEXT STEPS (v4.9.0)

1. Wire `TradeDecisionState` to GUIBroadcaster
2. Add INDEX_STRUCTURE profile for SPX/US30
3. Integrate shock detection with activity router
4. Add structure-based exits

---

## 🔧 v4.8.0 AUDIT FIXES (2025-01-01)

### FIX #1: Session Resolution Before Profile Selection (REQUIRED)

**PROBLEM:**
- Profile selection could occur before session resolution stabilized
- This caused correct symbol + correct microstructure but WRONG profile (LDN vs NY)
- Impact: Suppressed good trades (not bad trades)

**SOLUTION (ActivityRouter.hpp):**
- Added `SessionStability` struct with 30-second stability threshold
- Added `isSessionResolved(session, now_ns)` check
- `selectProfile()` now returns `DISABLED` until session is stable
- Prevents LDN/NY confusion during session transitions

### FIX #2: WAITING_FOR_TRIGGER State Visibility (REQUIRED)

**PROBLEM:**
- When all gates passed but no trigger fired, system waited silently
- From outside: "It should have traded but didn't"
- Operationally dangerous (can't distinguish gate failure from trigger waiting)

**SOLUTION (TradeDecisionState.hpp):**
- Added `VetoReason::WAITING_FOR_TRIGGER` enum value
- Added `VetoReason::SESSION_UNSTABLE` enum value
- Added `waiting_for_trigger` field to `TradeDecisionState`
- Added `session_stable` field to `TradeDecisionState`
- Updated JSON serialization and console output
- WHY-NOT-TRADING panel now shows "⏳ WAITING_FOR_TRIGGER" distinctly

### FIX #3: Time Cap Extension When Structure Resolving (OPTIONAL)

**PROBLEM:**
- If structure resolves slowly but never flags expansion, time stop fires early
- Impact: Small scratches, expectancy erosion over weeks

**SOLUTION (StructureExit.hpp):**
- Added `StructureResolvingState` enum: `NOT_RESOLVING`, `RESOLVING_SLOW`, `RESOLVING_ACTIVE`
- Added `TimeCap` struct with `+3s` extension when structure is resolving
- Added `shouldExitStructure()` function with time cap extension logic
- Added `ExitReason::TIME_CAP` for explicit tracking

### Files Modified:
- `include/core/ActivityRouter.hpp` - Session stability check
- `include/core/TradeDecisionState.hpp` - New fields + enum values
- `include/core/StructureExit.hpp` - Time cap extension logic

### Warning Fixes:
- `cfd_engine/include/CfdEngine.hpp` - Removed unused `mid` variable
- `include/core/ScalpProfile.hpp` - Added `(void)symbol;` to suppress unused parameter warnings

---

## 📊 NEW: Daily Health Audit Subsystem (v4.8.0)

### OVERARCHING GUARANTEE

After this is wired:
- You cannot keep trading a broken profile
- You cannot justify bad behavior with PnL
- You cannot forget to check system health
- Chimera protects itself before damage occurs

This is how real desks operate.

### Files Added:

| File | Purpose |
|------|---------|
| `include/audit/TradeRecord.hpp` | Trade record structure (symbol, PnL in R, duration, outcome) |
| `include/audit/DailyAuditReport.hpp` | Audit report with verdict (PASS/WARNING/FAIL) |
| `include/audit/DailyHealthAudit.hpp` | Core audit engine with singleton |
| `src/audit/DailyHealthAudit.cpp` | Audit implementation |
| `include/audit/ProfileGovernor.hpp` | Auto-throttling and profile disabling |
| `include/audit/DailyReportExporter.hpp` | JSON export for accountability |
| `include/audit/LiveHealthSnapshot.hpp` | Real-time dashboard feed |
| `include/audit/Audit.hpp` | Master include + workflow helpers |

### HARD RULES (NON-NEGOTIABLE):

| Rule | Threshold | Verdict |
|------|-----------|---------|
| Average loss | > 1.0R | FAIL |
| Payoff ratio | < 1.5 (with wins) | FAIL |
| Max single loss | > 1.2R | FAIL |
| Worst 3-trade DD | > 2.0R | FAIL |
| Losing duration | > 50% of winning | WARNING |
| Insane veto reasons | UNKNOWN/DEFAULT/FALLBACK | FAIL |

### USAGE:

```cpp
#include "audit/Audit.hpp"

// On trade close
getDailyAudit().recordTrade(tradeRecord);

// On entry veto
getDailyAudit().recordVeto(symbol, veto_reason);

// At end of session
DailyAuditReport report = getDailyAudit().runDailyAudit();
report.print();

// Apply enforcement (NON-NEGOTIABLE)
if (report.verdict == "FAIL") {
    getProfileGovernor().setState("SCALP_FAST", ProfileState::DISABLED);
}

// Or use the convenience function
runEndOfSessionAudit("SCALP_FAST");
```

### ProfileGovernor Enforcement:

```cpp
// Entry gate check
if (!getProfileGovernor().isAllowed(profileName)) {
    veto("PROFILE_DISABLED_BY_AUDIT");
}

// Size scaling when throttled
double size_mult = getProfileGovernor().getThrottleMultiplier(profileName);
```

### JSON Export:

Reports automatically exported to `logs/daily_audit_YYYY-MM-DD.json`

### Dashboard Feed:

```cpp
LiveHealthSnapshot snap = LiveHealthSnapshot::fromReport(report);
gui.publish("daily_health", snap);
```

---

## 📊 NEW: Rolling Edge Audit (v4.8.0)

### PURPOSE

Answers one question only: **Is this system's edge still alive over the last N sessions — even if daily audits pass?**

### PROTECTS AGAINST:
- Slow edge decay
- Regime drift
- Over-scratching
- "looks fine daily but dying monthly"

### FILES ADDED:

| File | Purpose |
|------|---------|
| `include/audit/RollingEdgeReport.hpp` | Report structure with verdict |
| `include/audit/RollingEdgeAudit.hpp` | Rolling edge audit engine (singleton) |
| `src/audit/RollingEdgeAudit.cpp` | Implementation |

### VERDICT LOGIC:

| Condition | Verdict |
|-----------|---------|
| edge_retention < 0.55 OR payoff < 1.3 OR DD > 3.0R | **BROKEN** |
| edge_retention < 0.65 OR payoff < 1.5 OR avg_pnl < 0 | **DEGRADING** |
| Otherwise | **HEALTHY** |

### USAGE:

```cpp
getRollingEdgeAudit().recordTrade(tradeRecord);  // On every trade close
RollingEdgeReport rep = getRollingEdgeAudit().evaluateProfile("SCALP_FAST");

if (rep.verdict == RollingEdgeVerdict::BROKEN) {
    getProfileGovernor().setState("SCALP_FAST", ProfileState::DISABLED);
}
```

---

## 🔄 NEW: Edge Recovery Rules (v4.8.0)

### PURPOSE

Automatic, conservative re-enablement of profiles **only after edge proves it has recovered**.

### RECOVERY PATH:

```
DISABLED → THROTTLED → ENABLED
```

**NEVER:** `DISABLED → ENABLED` directly

### FILES ADDED:

| File | Purpose |
|------|---------|
| `include/audit/EdgeRecoveryState.hpp` | Recovery state tracking |
| `include/audit/EdgeRecoveryRules.hpp` | Recovery rules engine (singleton) |
| `src/audit/EdgeRecoveryRules.cpp` | Implementation |

### RECOVERY THRESHOLDS:

| Transition | Requirements |
|------------|--------------|
| DISABLED → THROTTLED | 5 healthy sessions, 3 clean days, retention ≥ 0.65, payoff ≥ 1.6 |
| THROTTLED → ENABLED | 10 healthy sessions, 5 clean days, retention ≥ 0.70, payoff ≥ 1.7 |

### USAGE:

```cpp
getEdgeRecoveryRules().evaluate("SCALP_FAST", rollingReport, dailyReport, getProfileGovernor());
```

---

## 🚦 NEW: Go/No-Go Gate (v4.8.0)

### PURPOSE

Session start decision - **trade or don't trade**.

### IF NO_GO:
- No profiles trade
- No overrides
- No "just one trade"

### FILES ADDED:

| File | Purpose |
|------|---------|
| `include/audit/GoNoGoDecision.hpp` | Decision structure |
| `include/audit/GoNoGoGate.hpp` | Gate engine (singleton) |
| `src/audit/GoNoGoGate.cpp` | Implementation |

### BLOCKERS (in priority order):

1. `LATENCY_UNSTABLE` - Latency not stable
2. `NEWS_SHOCK_ACTIVE` - News/shock blackout active
3. `DAILY_AUDIT_FAIL` - Yesterday's daily audit failed
4. `NO_HEALTHY_PROFILES` - All profiles disabled/broken

### USAGE:

```cpp
GoNoGoDecision decision = getGoNoGoGate().evaluate(
    "NY", dailyReport, rollingReports,
    getProfileGovernor(), latencyStable, shockActive
);

if (decision.status == GoNoGoStatus::NO_GO) {
    disableAllTrading(decision.reason);
}
```

---

## 🏛️ Complete Governance Hierarchy

**Order of authority (highest → lowest):**

1. Latency / Shock / Risk exits
2. DailyHealthAudit (hard stop)
3. RollingEdgeAudit (slow throttle)
4. EdgeRecoveryRules (conservative re-enable)
5. GoNoGoGate (session start decision)
6. Strategy logic

### CONVENIENCE FUNCTIONS:

```cpp
#include "audit/Audit.hpp"

// End of session
runCompleteSessionAudit("SCALP_FAST");

// Start of session
GoNoGoDecision decision = checkSessionReadiness("NY", "SCALP_FAST", latencyOK, shockActive);

// Start of new day
startNewTradingDay();
```

---

## 🦅 NEW: Predator Ultra-Fast Scalping Profile (v4.8.0)

### CORE PHILOSOPHY

**Predator does NOT predict. It reacts faster than the market can lie.**

It only trades when:
- Structure is resolving
- Latency is clean
- Microstructure confirms immediately
- Invalidation is extremely tight

If conditions are not perfect → it does nothing.

### FILES ADDED:

| File | Purpose |
|------|---------|
| `include/profile/PredatorSymbolConfig.hpp` | Symbol-specific thresholds |
| `include/profile/PredatorSessionPolicy.hpp` | Session-based aggression scaling |
| `include/profile/PredatorIdleReason.hpp` | Idle reason tracking |
| `include/profile/PredatorProfile.hpp` | Main profile header |
| `src/profile/PredatorProfile.cpp` | Profile implementation |
| `include/micro/VwapAcceleration.hpp` | VWAP slope acceleration filter |
| `include/risk/LossVelocity.hpp` | Adaptive cooldown + consecutive loss tracker |
| `include/audit/PredatorExpectancy.hpp` | Per-symbol expectancy dashboard |

### SYMBOL TABLE (AUTHORITATIVE):

| Symbol  | Imbalance ≥ | Accept ms | Edge Exit | Max Hold | Notes              |
|---------|-------------|-----------|-----------|----------|--------------------| 
| NAS100  | 0.75        | ≤120ms    | 60%       | 1.8s     | Best speed edge    |
| US30    | 0.70        | ≤150ms    | 55%       | 2.0s     | Noisier, looser    |
| SPX500  | 0.80        | ≤100ms    | 65%       | 1.5s     | Clean but selective|
| XAUUSD  | 0.72        | ≤180ms    | 50%       | 2.5s     | Needs more room    |

### SESSION MATRIX:

| Session  | Aggression | Risk Mult | Max Trades |
|----------|------------|-----------|------------|
| NY_OPEN  | FULL       | 1.0×      | 6          |
| NY_MID   | REDUCED    | 0.6×      | 3          |
| LDN      | REDUCED    | 0.5×      | 3          |
| ASIA     | OFF        | 0×        | 0          |

### ENTRY TYPES:

**TYPE A — IMBALANCE SNAPBACK (Fade Failure)**
- OrderBookImbalance ≥ 0.75
- Price fails to continue within 120ms
- Book refills ≥ 65% inside 200ms
- VWAP slope flattens or reverses
- → Enter against the failed imbalance

**TYPE B — MICRO BREAK + IMMEDIATE ACCEPTANCE**
- Micro range break (last 500–800ms)
- Follow-through ≥ 2 ticks within 150ms
- No VWAP rejection
- → Enter with the break

### RISK MODEL (NON-NEGOTIABLE):

| Parameter | Value |
|-----------|-------|
| Risk per trade | 0.05% – 0.10% |
| Max positions | 1 |
| Scaling | NO |
| Averaging | NO |
| Max hold | 1.5s – 2.5s (symbol dependent) |

### EXIT TRIGGERS (FIRST HIT WINS):

1. Time cap (symbol specific)
2. Edge decay > threshold
3. Imbalance flip against position
4. VWAP micro reclaim
5. Latency degradation

### SAFETY OVERRIDES (ABSOLUTE):

- 2 consecutive losses → Predator auto-disabled
- RollingEdgeVerdict == WARNING → aggression forced to REDUCED
- RollingEdgeVerdict == BROKEN → Predator DISABLED
- DailyAudit FAIL → Predator DISABLED

### ADAPTIVE FEATURES:

**VWAP Acceleration Filter:**
- Only trades when VWAP slope is accelerating
- Removes chop, false acceptance, fake breaks

**Loss Velocity Cooldown:**
- Cooldown = base (5s) + (losses in 10min × 3s)
- Max cooldown: 20s
- Prevents revenge sequences

**Per-Symbol Expectancy Dashboard:**
- Avg loss/win size
- Win/Loss ratio
- Avg time in losing trades
- Real-time health status

### DEPLOYMENT CHECKLIST:

Before enabling PREDATOR LIVE:
- [ ] GoNoGoGate = GO
- [ ] RollingEdgeAudit = HEALTHY
- [ ] Latency stable ≥ 99%
- [ ] Asia session = OFF
- [ ] Max risk = 0.05–0.10%
- [ ] Loss velocity tracker active
- [ ] Expectancy dashboard visible

If all green → enable.
