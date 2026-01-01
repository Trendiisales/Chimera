# Chimera v4.3.4 - STATE==RUNNING Check + CapitalPolicy

## Critical Trading Gate Fix

Added `state_ == SymbolState::RUNNING` check to gate_pass - trades were firing during INIT state.

## NEW: CapitalPolicy System (CFD Engine)

Added institutional-grade capital management:

### Symbol Tiering
- **Tier A** (full capital): NAS100, SPX500, US30, XAUUSD
- **Tier B** (reduced): GER40, UK100, EURUSD, GBPUSD, USDJPY
- **Tier C** (blocked): Everything else

### Money Windows Only
- London Open: 07:00-09:00 UTC (1.4x multiplier)
- London-NY: 12:00-14:00 UTC (1.2x multiplier)
- NY Open: 13:30-15:30 UTC (1.6x multiplier)
- Other times: BLOCKED

### Overlapping Index Protection
Cannot hold NAS100 + US30 same direction unless one is risk-free.

### Default = NO TRADE
Every decision logged with explicit block reason:
```
[CAPITAL-POLICY] NAS100 session=LONDON_OPEN allowed=NO reason=SPREAD_WIDE
[CAPITAL-POLICY] XAUUSD session=NY_OPEN allowed=YES risk=0.8% [SCALE-UP]
```

### Scale-Up Rules
- Only after +0.5R unrealized
- Stop must be at break-even
- One add maximum (+50%)
- Must be in money window

## Files Added/Modified
- `cfd_engine/include/config/CapitalPolicy.hpp` (NEW)
- `crypto_engine/include/binance/SymbolThread.hpp` (STATE check)
- `src/main_dual.cpp` (version bump)
