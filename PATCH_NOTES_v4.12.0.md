# CHIMERA v4.12.0 - DECISION VISIBILITY PATCH
## Applied: 2025-01-27

## OVERVIEW
This patch adds explicit decision emission logging and strategy-aware regime policy framework to Chimera. It ensures that all trading decisions are clearly visible in the console output and provides the infrastructure for strategy-specific regime gates.

## FILES CHANGED

### 1. NEW FILE: include/execution/TradePolicy.hpp
**Purpose**: Strategy-aware regime gating framework

**Key Features**:
- Defines Regime enum (INVALID, STABLE, SKEW, TREND, VOLATILE, DEGRADED)
- Defines StrategyType enum (FADE, MOMENTUM, BREAKOUT)
- Provides tradeAllowed() method for strategy-regime compatibility:
  * FADE strategies → trade in STABLE, SKEW regimes
  * MOMENTUM/BREAKOUT → trade in TREND, VOLATILE regimes
- Helper methods for regime/strategy name conversion

**Usage**: Foundation for future strategy-aware regime filtering

### 2. MODIFIED: src/FadeETH.cpp
**Lines Changed**: ~427-443

**Change**: Added explicit [DECISION] logging before router call

**Before**:
```cpp
router_->route(
    "ETHUSDT",
    verdict.expected_move_bps,
    bid, ask, intent.quantity,
    want_long, now_ns, regime_str
);
```

**After**:
```cpp
// Explicit decision emission log
std::cout << "[DECISION] symbol=ETHUSDT"
          << " edge=" << verdict.expected_move_bps
          << " qty=" << intent.quantity
          << " regime=" << regime_str
          << " strategy=FADE"
          << std::endl;

router_->route(
    "ETHUSDT",
    verdict.expected_move_bps,
    bid, ask, intent.quantity,
    want_long, now_ns, regime_str
);
```

**Impact**: Every decision is now explicitly logged before routing

### 3. MODIFIED: src/FadeSOL.cpp
**Lines Changed**: ~415-431

**Change**: Same as FadeETH - added explicit [DECISION] logging

**Impact**: Consistent decision visibility across both FADE strategies

### 4. MODIFIED: src/execution/DecisionRouter.cpp
**Lines Changed**: ~19-38

**Change**: Enhanced logging and clarified shadow mode logic

**Key Improvements**:
- More explicit blocking messages with symbol names
- Added "DECISION ACCEPTED" log showing mode, edge, qty
- Clarified comments about shadow mode behavior
- Ensures edge=0 passes through in shadow mode

**Before**:
```cpp
if (qty <= 0.0) {
    std::cout << "[ROUTER] ⚠️  Invalid qty=" << qty << " - skipping" << std::endl;
    return;
}
```

**After**:
```cpp
if (qty <= 0.0) {
    std::cout << "[ROUTER] ⚠️  BLOCKED InvalidQty: qty=" << qty << " symbol=" << symbol << std::endl;
    return;
}

// ... similar improvements for other gates ...

std::cout << "[ROUTER] ✅ DECISION ACCEPTED: symbol=" << symbol
          << " edge=" << edge_bps
          << " qty=" << qty
          << " mode=" << (mode_ == ExecutionMode::SHADOW ? "SHADOW" : "LIVE")
          << std::endl;
```

## WHAT YOU'LL SEE IN LOGS

### Before This Patch:
```
[FadeETH] 🔥 SIGNAL GENERATED (ECONOMICALLY APPROVED)
  Expected Move: 1.234 bps
[FadeETH] 🔥 SHADOW DECISION: BUY expected=1.234bps
[ROUTER] 🎯 ORDER ROUTED: ETHUSDT BUY qty=0.123 edge=1.234bps #42
```

### After This Patch:
```
[FadeETH] 🔥 SIGNAL GENERATED (ECONOMICALLY APPROVED)
  Expected Move: 1.234 bps
[DECISION] symbol=ETHUSDT edge=1.234 qty=0.123 regime=STABLE strategy=FADE
[ROUTER] ✅ DECISION ACCEPTED: symbol=ETHUSDT edge=1.234 qty=0.123 mode=SHADOW
[FadeETH] 🔥 SHADOW DECISION: BUY expected=1.234bps
[ROUTER] 🎯 ORDER ROUTED: ETHUSDT BUY qty=0.123 edge=1.234bps #42
[SHADOW_EXEC] 🔶 OPEN ETHUSDT BUY qty=0.123 @ 3007.76 TP=3009.84 SL=3006.12
```

## BENEFITS

1. **Decision Visibility**: Every decision is now explicitly marked with [DECISION] tag
2. **Router Confirmation**: Router acceptance is explicitly logged with mode info
3. **Debugging Aid**: Can grep logs for "[DECISION]" to see all decision points
4. **Shadow Mode Clarity**: Mode is shown in router logs for verification
5. **Strategy-Aware Framework**: TradePolicy.hpp provides foundation for future enhancements

## NO BEHAVIORAL CHANGES

**IMPORTANT**: This patch does NOT change trading logic or blocking behavior. It only:
- Adds logging visibility
- Creates infrastructure for future strategy-aware filtering
- Clarifies existing shadow mode behavior

The existing logic remains:
- EV gate still blocks low-edge trades in LIVE mode, allows in SHADOW
- Regime INVALID still hard-blocks all trades
- ShadowExecutionEngine still opens/closes positions based on TP/SL/timeout

## NEXT STEPS

To fully implement strategy-aware regime gates, you would:

1. Replace RegimeState enum with TradePolicy::Regime enum
2. Modify FadeETH/FadeSOL to use TradePolicy::tradeAllowed()
3. Update regime classification to use microstructure signals (not just latency)

## VERIFICATION

After deployment, verify patch is working by checking logs for:
```
grep "[DECISION]" logs/chimera.log
```

You should see one [DECISION] line for every signal that passes the EV gate.

## DEPLOYMENT NOTES

- This is a SAFE patch (logging only + infrastructure)
- No retuning required
- No settings changes needed
- Backward compatible with existing regime system
- Ready for immediate deployment to production

---
**Version**: 4.12.0  
**Patch Date**: 2025-01-27  
**Author**: Claude + Jo  
**Status**: READY FOR DEPLOYMENT
