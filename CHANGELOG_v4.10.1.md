# CHANGELOG v4.10.1 - Gold Disabled Pending Backtest

## Date: 2025-01-06

## Summary
Surgical modification to disable Gold trading while preserving all code for future re-enablement after proper backtest validation.

## Problem Statement
v4.10.0 integrated GoldCampaignEngine into the live execution path despite Gold not meeting the proven engine criteria:
- Win rate: Not validated
- Profit factor: < 1.25 required
- Max DD: Not validated
- Expectancy: Negative or unknown

This violated the portfolio discipline rules that only proven engines should receive capital.

## Changes Made

### `/include/portfolio/PortfolioModeController.hpp`
- Added `GOLD_ENABLED = false` compile-time constant
- Added `GOLD_DISABLE_REASON = "DISABLED_PENDING_BACKTEST"`
- `decideMode()` now always returns `INDEX_PRIORITY` when `GOLD_ENABLED=false`
- `registerSymbol()` immediately disables Gold symbols with reason
- `updateSymbolAllocation()` hard-blocks Gold regardless of mode
- Added `isGoldEnabled()` and `getGoldDisableReason()` static methods
- All Gold code paths preserved but unreachable

### `/include/integration/CfdEngineIntegration.hpp`
- Removed `XAUUSD` from symbol registration
- `onTick()` returns immediately with `GOLD_DISABLED` for Gold symbols
- `onBarClose()` skips Gold bar processing
- `processGold()` function preserved but commented out
- Added prominent Gold disable status in `printStatus()`
- Added `isGoldEnabled()` static method

## What Is NOT Changed
- `GoldCampaignEngine.hpp` - fully preserved, just not called
- `MarketQualityCuts.hpp` - Gold cuts preserved for future use
- All Gold-related structs and enums - preserved for ABI compatibility
- GOLD_PRIORITY mode enum - preserved but string shows "[DISABLED]"

## Re-Enabling Gold
To re-enable Gold after successful backtest validation:

1. In `PortfolioModeController.hpp`:
   ```cpp
   static constexpr bool GOLD_ENABLED = true;
   ```

2. In `CfdEngineIntegration.hpp`:
   - Uncomment `portfolio.registerSymbol("XAUUSD");`
   - Uncomment Gold routing in `onBarClose()`
   - Uncomment `processGold()` implementation

3. Recompile

## Portfolio State After v4.10.1

| Symbol   | Status   | Risk    | Notes |
|----------|----------|---------|-------|
| NAS100   | ENABLED  | 0.6%    | Core engine |
| US100    | ENABLED  | 0.6%    | Core engine (alias) |
| US30     | ENABLED  | 0.5%    | Core engine |
| SPX500   | ENABLED  | 0.4%    | Core engine |
| XAUUSD   | DISABLED | 0.0%    | Pending backtest |
| EURUSD   | ENABLED  | 0.3%    | FX (uses PureScalper) |
| GBPUSD   | ENABLED  | 0.3%    | FX (uses PureScalper) |
| USDJPY   | ENABLED  | 0.3%    | FX (uses PureScalper) |

## Mode Decision
- v4.10.1: Always `INDEX_PRIORITY`
- Gold signals ignored in mode decision
- No GOLD_PRIORITY mode activation possible

## Verification
After compile, logs should show:
```
[PORTFOLIO] *** GOLD DISABLED: DISABLED_PENDING_BACKTEST ***
[CFD-INTEGRATION] *** GOLD DISABLED: DISABLED_PENDING_BACKTEST ***
```

Gold ticks should return:
```
reason = "GOLD_DISABLED"
```

## Rationale
This follows the stated portfolio discipline:
> "Gold must be CUT. No new campaigns. Minimal live-ready portfolio."

Engineering quality is preserved - this is surgical disablement, not removal.
