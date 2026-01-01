# Chimera v4.7.0 - EXECUTION AUTHORITY + DUAL SCALP

## Release Date: 2025-01-01

## Overview

This release implements two critical systems:
1. **ExecutionAuthority** - THE SINGLE CHOKE POINT for all execution
2. **ScalpProfile** - DUAL SCALP (NY + LONDON) activity profiles

### The Prime Directive

```
═══════════════════════════════════════════════════════════════════════════════
                         🔒 THE PRIME DIRECTIVE 🔒
    
    Chimera is allowed to lose money.
    Chimera is NOT allowed to trade without intent.

    NO ORDER MAY BE SENT UNLESS INTENT == LIVE
═══════════════════════════════════════════════════════════════════════════════
```

---

## Part 1: EXECUTION AUTHORITY

### ExecutionAuthority.hpp - THE SINGLE CHOKE POINT

Every order must pass through `ExecutionAuthority::allow()` before execution:

```cpp
ExecBlockReason block_reason;
if (!getExecutionAuthority().allowCrypto(symbol, intent_is_live, &block_reason)) {
    // BLOCKED - log reason, return
}
// ALLOWED - proceed with execution
```

### Check Order (all must pass):

1. **INTENT CHECK** - Is intent LIVE? (FIRST AND MOST IMPORTANT)
2. **SYMBOL CHECK** - Is symbol allowed? (tier classification)
3. **FIX CHECK** - Is FIX connected? (CFD only)
4. **EXPANSION CHECK** - Is NY expansion active? (opportunistic symbols only)
5. **LATENCY CHECK** - Is latency normal? (degraded = blocked)
6. **SHOCK CHECK** - Is market shock clear?
7. **RISK CHECK** - Does risk system allow?

### Integration Points:

- **BinanceOrderSender.hpp** - ExecutionAuthority FIRST, then RiskGovernor
- **FIXSession.hpp** - ExecutionAuthority FIRST, then RiskGovernor  
- **main_triple.cpp** - Wires intent to both engines every 50ms

### Intent Wiring:

```cpp
bool risk_allows = g_daily_loss.allow();
bool intent_is_live = binance_connected && ctrader_connected && risk_allows;

binance_engine.setIntentLive(intent_is_live);
cfd_engine.setIntentLive(intent_is_live);
getExecutionAuthority().setRiskAllows(risk_allows);
```

---

## Part 2: DUAL SCALP PROFILE SYSTEM

### ScalpProfile.hpp - SAME ENGINE, DIFFERENT TOLERANCES

Three logical modes, two activity profiles:

| Profile    | Session | Purpose                          |
|------------|---------|----------------------------------|
| CORE       | Any     | Rare, structural, big edge       |
| SCALP-NY   | NY      | Aggressive, momentum + continuation |
| SCALP-LDN  | London  | Controlled, range + breakout scalps |

### Session Classification (Mandatory):

```cpp
enum class Session {
    ASIA,           // No scalp
    LONDON,         // SCALP-LDN
    NY_OPEN,        // SCALP-NY
    NY_CONTINUATION,// SCALP-NY
    OFF_HOURS       // No scalp
};
```

### SCALP-NY (Aggressive, Momentum)

**Entry Rules (ALL required):**
- regime != TOXIC
- edge >= base_edge (NAS100: 0.55, XAUUSD: 0.60)
- persistence >= min (NAS100: 0.40, XAUUSD: 0.45)
- imbalance aligned OR momentum burst
- latency == NORMAL
- shock == CLEAR

**Exit Rules (FAST):**
- Edge decay < 70% of entry
- Latency != NORMAL → EXIT
- Time > 3.5s & not profitable → EXIT
- Shock → Immediate

**Risk:**
```cpp
risk = 0.30 × CORE
max_positions = 1  // No pyramids
```

### SCALP-LDN (Controlled, Range-Aware)

**Entry Rules (ALL required):**
- regime == STABLE or TRANSITION
- edge >= base_edge (NAS100: 0.65, XAUUSD: 0.70)
- persistence >= min (NAS100: 0.50, XAUUSD: 0.55)
- spread <= median_spread × 1.15
- range_expansion < 1.8
- latency == NORMAL
- shock == CLEAR

**Exit Rules (TIGHTER):**
- Edge decay < 80% of entry
- Latency != NORMAL → EXIT
- Range expansion > 2.0 adverse → EXIT
- Time > 2.5s & not profitable → EXIT
- Shock → Immediate

**Risk:**
```cpp
risk = 0.20 × CORE
max_positions = 1  // No pyramids
```

### Daily Limits (HARD)

**SCALP:**
```cpp
max_loss_scalp_day = -0.50R (CORE risk unit)
max_trades_day = 25
max_consecutive_losses = 5
```

If ANY trigger: SCALP → DISABLED, CORE → UNCHANGED

**CORE:**
```cpp
max_loss_core_day = -1.00R
```

### What We Do NOT Allow (IMPORTANT)

❌ No SCALP in Asia  
❌ No SCALP during shock cooldown  
❌ No SCALP on ELEVATED latency  
❌ No pyramids in SCALP  
❌ No FX in SCALP (yet)

### Observability - "WHY NOT TRADING"

```
╔════════════════════════════════════════════════════════════╗
║  SCALP STATUS                                              ║
╠════════════════════════════════════════════════════════════╣
║  SYMBOL:  NAS100                                           ║
║  SESSION: LONDON                                           ║
║  PROFILE: SCALP-LDN                                        ║
╠════════════════════════════════════════════════════════════╣
║  Edge:        0.72 / 0.65 ✔                                ║
║  Persistence: 0.53 / 0.50 ✔                                ║
║  Spread:      OK ✔                                         ║
║  Latency:     NORMAL ✔                                     ║
║  Range:       TOO WIDE ✖                                   ║
╠════════════════════════════════════════════════════════════╣
║  BLOCKER: RANGE_EXPANSION ✖                                ║
╚════════════════════════════════════════════════════════════╝
```

---

## Exact Base Numbers (NO TUNING GUESSWORK)

### NAS100

| Param         | SCALP-NY | SCALP-LDN |
|---------------|----------|-----------|
| base_edge     | 0.55     | 0.65      |
| persistence   | 0.40     | 0.50      |
| imbalance_min | 0.15     | -         |
| spread_max    | -        | ×1.15     |
| range_cap     | -        | 1.80      |
| time_cap_sec  | 3.5      | 2.5       |
| edge_decay    | 0.70     | 0.80      |

### XAUUSD (Gold lies more)

| Param         | SCALP-NY | SCALP-LDN |
|---------------|----------|-----------|
| base_edge     | 0.60     | 0.70      |
| persistence   | 0.45     | 0.55      |
| imbalance_min | 0.18     | -         |
| spread_max    | -        | ×1.10     |
| range_cap     | -        | 1.70      |
| time_cap_sec  | 3.0      | 2.0       |
| edge_decay    | 0.75     | 0.80      |

---

## Expected Day-1 Behavior

### Best-case:
- 6-15 SCALP trades
- Many scratches
- 1-2 clean winners
- Flat to slightly green day

### Acceptable:
- 2-5 trades
- Small net loss
- SCALP stops early
- CORE untouched

### Red flag (systemic):
- Trades firing every minute
- Large losses per trade
- Latency-blocked trades still entering

**If you see red flags → stop and review logs, do not tune.**

---

## Files Changed

### NEW (include/core/):
- **ExecutionAuthority.hpp** - THE single choke point
- **ShockDetector.hpp** - Session-aware market shock detection
- **StructureExit.hpp** - Structure-based exits (no TP/SL/time)
- **ScalpProfile.hpp** - DUAL SCALP system (NY + LONDON)

### MODIFIED:
- **BinanceOrderSender.hpp** - ExecutionAuthority FIRST gate + setIntentLive()
- **FIXSession.hpp** - ExecutionAuthority FIRST gate + setIntentLive()
- **BinanceEngine.hpp** - setIntentLive() exposed
- **CTraderFIXClient.hpp** - setIntentLive() forwarded
- **CfdEngine.hpp** - setIntentLive() forwarded + v4.7.0 intent policy logging
- **main_triple.cpp** - Intent wiring + ScalpProfile init + daily status logging

---

## Mental Model (IMPORTANT)

If you think:
> "This could trade more"

That usually means:
> "The market is not offering clean edge"

**Do not fight that.**

---

## Summary

1. **ExecutionAuthority** - Intent check FIRST, then symbol/FIX/expansion/latency/shock/risk
2. **ScalpProfile** - SCALP-NY (aggressive) + SCALP-LDN (controlled)
3. **Daily Limits** - SCALP dies quickly on bad days, CORE survives

**EXECUTION LAW: Intent != LIVE → NOTHING passes. NO EXCEPTIONS.**
