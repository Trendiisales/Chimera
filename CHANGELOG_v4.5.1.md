# Chimera v4.5.1 Changelog

## Release Date: 2024-12-30

## Overview
v4.5.1 implements two critical systems:
1. **NAS100 Time-Based Ownership System** - clean separation between IncomeEngine and CFDEngine
2. **Global Risk Governor** - hard daily loss cap at -$200 NZD with aggression control

---

## 🔒 GLOBAL RISK GOVERNOR (NON-NEGOTIABLE)

### Hard Daily Loss Cap: -$200 NZD
```cpp
if (daily_realised_pnl <= -200 NZD)
    shutdown_all_engines("DAILY_MAX_LOSS");
```

**This is your circuit breaker. Nothing overrides it.**

### Implementation
- **Fast check every 50ms** in main loop (not 60 seconds)
- **Per-order enforcement** via `GlobalRiskGovernor::canSubmitOrder()`
- **Immediate shutdown** - kills all engines, no re-arming same day

### Engine Risk Limits (Fixed - No Intraday Changes)
| Engine | Max Risk per Trade |
|--------|-------------------|
| IncomeEngine | 0.5% |
| CFDEngine | 0.25% |
| CryptoEngine | 0.05% |

**No doubling. No martingale. Ever.**

### Aggression Control (Replaces Profit Caps)
**IncomeEngine outcome drives the day:**
- Income WIN → CFDEngine full allocation, CryptoEngine allowed
- Income SCRATCH → CFDEngine 50%, CryptoEngine disabled
- Income LOSS → All engines stand down (PROTECTION day)

### Drawdown Throttle Curve
| DD Used | Size Multiplier |
|---------|-----------------|
| 0% | 100% |
| 25% (-$50) | ~70% |
| 50% (-$100) | ~40% |
| 75% (-$150) | ~15% + block new entries |
| 100% (-$200) | KILL EVERYTHING |

### Auto-Shutdown Conditions
- Daily realised PnL ≤ -$200 NZD
- Two consecutive losses across engines
- Execution latency degrades
- Ownership violation (engine overlap)
- Manual panic trigger

---

## Key Features

### 1. Time-Based NAS100 Ownership
The core behavioral contract is now enforced at the execution layer:

**Income Window (03:00-05:00 NY):**
- IncomeEngine has EXCLUSIVE ownership of NAS100
- CFDEngine is HARD BLOCKED from NAS100 trading
- One trade per session, locks after exit

**Outside Income Window:**
- CFDEngine owns NAS100
- IncomeEngine is blocked
- Different trading rules apply (breakouts, momentum, faster scratches)

### 2. CFD Wind-Down Before Income Window
- **T-10 minutes**: CFDEngine blocked from new NAS100 entries
- **T-5 minutes**: CFDEngine must force-flat any NAS100 positions
- Prevents overlap with income window

### 3. Execution Guard (Non-Negotiable)
```cpp
// THE CRITICAL GUARD - Put this inside submitOrder() for NAS100
if (symbol == "NAS100" && !Chimera::canTradeNAS100(engine)) {
    LOG("[ENGINE-BLOCK] engine=%d symbol=NAS100 ownership", (int)engine);
    return;  // HARD STOP
}
```

This guard lives at the execution boundary and makes mistakes IMPOSSIBLE, regardless of signal logic.

### 4. Dashboard NAS100 Ownership Panel
New panel showing:
- Current owner (INCOME / CFD / NONE)
- NY time
- Status message (countdown to income window, wind-down status)
- Income window active indicator
- CFD wind-down indicator
- Income locked indicator

### 5. GUI WebSocket Data
New `nas100_ownership` object in JSON stream:
```json
{
  "nas100_ownership": {
    "owner": "CFD",
    "income_window_active": false,
    "cfd_no_new_entries": false,
    "ny_time": "22:15",
    "seconds_to_income": 17100,
    "seconds_in_income": 0,
    "cfd_forced_flat_seconds": 0,
    "income_locked": false
  }
}
```

## Configuration Updates

### New Demo Account
Updated cTrader credentials:
- Host: `demo-uk-api-01.p.c-trader.com`
- SenderCompID: `demo.blackbull.2082409`
- Username: `2082409`

### Income Window Configuration
```cpp
struct IncomeWindowConfig {
    int start_hour = 3;                    // 03:00 NY
    int end_hour = 5;                      // 05:00 NY
    int cfd_no_new_entries_minutes = 10;   // T-10 min
    int cfd_forced_flat_minutes = 5;       // T-5 min
    bool income_locks_after_exit = true;
};
```

## NAS100 Ownership Schedule (NY Time)

| NY Time | Income | CFD | Notes |
|---------|--------|-----|-------|
| 18:00-02:00 | OFF | ON | Asia session, small size |
| 02:00-02:50 | OBSERVE | ON | London prep |
| 02:50-03:00 | OBSERVE | WIND-DOWN | No new CFD entries |
| 03:00-05:00 | EXCLUSIVE | HARD OFF | Income window |
| 05:00-10:00 | LOCKED | ON | Post-income, NY open |

## Files Changed

### Core
- `include/core/EngineOwnership.hpp` - Complete rewrite with time-based NAS100 ownership
- `config.ini` - Updated to new demo account

### Integration
- `src/main_triple.cpp` - Added NAS100 ownership monitoring, status output, daily state reset
- `include/gui/GUIBroadcaster.hpp` - Added NAS100 ownership JSON data

### Dashboard
- `chimera_dashboard.html` - Added NAS100 Ownership Panel with live status

## API Reference

### New Functions
```cpp
// Check if engine can trade NAS100 NOW
bool canTradeNAS100(EngineId engine);

// Check if income window is active
bool isIncomeWindowActive();

// Check if CFD is in wind-down period
bool isCFDNAS100WindDown();

// Check if CFD must force-flat NAS100
bool isCFDNAS100ForcedFlat();

// Get current NAS100 owner
NAS100Owner getNAS100Owner();

// Get full ownership state for dashboard
NAS100OwnershipState getNAS100OwnershipState();
```

### Income Engine Lock
```cpp
// Lock income engine after trade exit
EngineOwnership::instance().lockIncomeEngine();

// Reset daily state (call at session start)
EngineOwnership::instance().resetDailyState();
```

## Phase-In Plan

### Phase 1 (Week 1-2)
- IncomeEngine NAS only
- CFD trades other symbols
- Validate income stability

### Phase 2 (Week 3-4)
- Enable CFD-NAS outside income window
- Size at 25-50% of Income
- Monitor overlap blocks (must be zero)

### Phase 3 (Week 5+)
- Consider size increase if stats justify
- Keep hard caps unchanged

## Automatic Failure Conditions

Disable CFD-NAS if any occur:
- Any trade during income window
- Any overlap with IncomeEngine position
- Daily CFD-NAS loss exceeds cap
- Repeated ownership blocks (logic regression)

## Final Posture

- **IncomeEngine = sniper** (rare, protected, boring)
- **CFDEngine = soldier** (opportunistic, capped)
- **Execution layer = referee** (rules win)
