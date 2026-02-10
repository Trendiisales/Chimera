# CHIMERA v4.35 - FINAL PRODUCTION BUILD

## What Was Removed

### Dead GUI Code (114KB → 1.4KB)
- ❌ NoTradeReason.hpp (17KB) - Replaced by RejectionReason in SymbolExecutor
- ❌ TradeDecision.hpp (20KB) - Not used
- ❌ NAS100 ownership tracking - Only doing metals
- ❌ US30 regime code - Only doing metals
- ❌ Engine ownership logic - Not used

### What We Kept (And Why)

**RejectionReason** (in SymbolExecutor.hpp)
- ✅ Tracks why trades are blocked
- ✅ Types: SESSION_BLOCKED, COST_GATE_FAILED, COOLDOWN_ACTIVE, etc.
- ✅ Logs rejection count and reasons
- ✅ Essential for understanding system behavior

**RejectionStats** (in SymbolExecutor.hpp)
- ✅ Counts total rejections
- ✅ Stores last rejection with value/threshold
- ✅ Printed in status() output

## Current System

**Symbols:** XAUUSD, XAGUSD only
**Mode:** Shadow (real costs)
**GUI:** Minimal stubs (62 lines)
**Rejection tracking:** Built-in via RejectionReason enum

## Console Output

```
[XAUUSD] EDGE_GATE BLOCKED: insufficient edge vs cost
[XAGUSD] HOURLY LIMIT: 4/4 BLOCKED
[XAUUSD] ENTRY trade_id=1 price=2750.50 size=1.0 (1/12 this hour)
[XAUUSD] EXIT STOP trade_id=1 pnl=$-28.15
[XAUUSD] legs=0 pnl=$-28.15 trades=1 rejects=5 hour=1/12
```

## Why Old Files Were Removed

**NoTradeReason.hpp** was for the old bloated system with:
- NAS100 ownership
- Engine routing
- US30 regimes
- Multiple strategies

**We replaced it with:**
- Simple RejectionReason enum
- 8 rejection types
- Built into SymbolExecutor
- Logs why trades blocked

**TradeDecision.hpp** was for:
- Complex multi-engine routing
- Confidence gates
- VWAP checks
- Regime filters

**We replaced it with:**
- EdgeGate (cost vs edge)
- DrawdownGate (risk bound)
- SessionGuard (overnight)
- MetalProfiles (XAU/XAG)

## Files Remaining

```
include/
  core/
    TradeLedger.hpp
  execution/
    ExecutionGovernor.hpp
  risk/
    CostModel.hpp
    EdgeGate.hpp
    DrawdownGate.hpp
    SessionGuard.hpp
  symbols/
    MetalProfiles.hpp
  shadow/
    SymbolExecutor.hpp (with RejectionReason)
    MultiSymbolExecutor.hpp
    ShadowTypes.hpp
    ShadowConfig.hpp
    CrashHandler.hpp
    WatchdogThread.hpp
    JournalWriter.hpp
    EquityCurve.hpp
  gui/
    GUIBroadcaster.hpp (62 lines)
  shared/
    GlobalRiskGovernor.hpp
```

## Total Size

- **Before refactor:** 2.47MB (245 files)
- **After refactor:** 376KB (clean, minimal)
- **GUI alone:** 114KB → 1.4KB (-99%)

## What You Get

✅ EdgeGate enforces fees
✅ DrawdownGate prevents overruns
✅ SessionGuard eliminates overnight
✅ RejectionReason tracks why trades blocked
✅ XAG trades 70% less than XAU
✅ Latency spikes blocked (convex penalty)
✅ Clean logs (percentiles)
✅ No dead code
✅ No NAS/US30 bloat

This is the minimal, correct system for XAUUSD/XAGUSD metals trading.
