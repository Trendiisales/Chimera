# CHANGELOG v4.10.0 - Index Impulse + Gold Campaign Integration

## Date: 2025-01-06

## Summary
Major release integrating proven backtest engines into Chimera CFD system:
- **Index Impulse Engine** for NAS100, US30, SPX500
- **Gold Campaign Engine** for XAUUSD
- **Portfolio Mode Controller** for hierarchical capital allocation
- **Market Quality Cuts** for structural filtering

## New Files

### `/include/quality/MarketQualityCuts.hpp`
Structural filter layer that removes ~45% of losing trades:
- ATR regime filter (skip high volatility trend days)
- Opening range percentile (skip dead/explosive opens)
- Failed break timing (skip slow bleed reversions)
- Compression quality (skip fake consolidations)
- Asia balance for Gold/FX (skip trend days)
- FX sweep timing (skip late fake sweeps)

### `/include/engines/IndexImpulseEngine.hpp`
Primary alpha engine for indices:
- **Engine 1 (E1)**: Compression → Expansion impulse detection
  - Entry: NY session 09:30-10:15 only
  - Pre-condition: Rolling range < ATR(20) × 0.6
  - Impulse: True range > rolling_range × 1.5, close in top/bottom 25%
  - Risk: 0.8% per trade
  - Exit: TRAILING ONLY (no TP, no time exit)
  - Trail stages: INITIAL → VWAP (+1R) → SWING (+2R) → AGGRESSIVE (+3R)

- **Engine 2 (E2)**: Continuation engine
  - Only trades AFTER E1 fires
  - Entry: VWAP pullback (within 0.2 × ATR)
  - Risk: 0.4% (50% of E1)
  - Same trailing exit logic

### `/include/engines/GoldCampaignEngine.hpp`
Gold directional campaign engine:
- **Engine G1**: Campaign detector (NOT a trading engine)
  - Detects campaign days vs dead range days
  - Conditions: NY range > Asia × 1.2, displacement > NY range × 0.8
  - Output: bias (LONG/SHORT), campaign (TRUE/FALSE)

- **Engine G2**: Campaign entry
  - Only trades if G1.campaign == TRUE
  - Entry: VWAP pullback with hold confirmation
  - Stop: Below last pullback (breathing stop)
  - Exit: TRAILING ONLY
  - Risk: 0.6% per campaign

### `/include/portfolio/PortfolioModeController.hpp`
Hierarchical portfolio orchestration:
- **Mode Selection** (at 12:15 UTC / 08:15 NY):
  - GOLD_PRIORITY if: Asia range elevated OR pre-NY displacement OR VWAP directional
  - INDEX_PRIORITY otherwise

- **Capital Allocation**:
  - INDEX_PRIORITY: NAS100=0.6%, US30=0.5%, SPX500=0.4%, Gold=OFF, FX=0.3%
  - GOLD_PRIORITY: Gold=0.8%, Indices=0.25% each, FX=OFF

- **Kill-Switches**:
  - Daily loss -1.5% → HALT ALL
  - DD 3% → reduce risk ×0.7
  - DD 5% → reduce risk ×0.4
  - DD 7% → HARD STOP
  - Correlation breaker: Gold + ≥2 index losses same direction → disable indices

### `/include/integration/CfdEngineIntegration.hpp`
Integration layer tying everything together:
- Initialization with starting equity
- Mode decision at 12:15 UTC
- Tick routing by symbol class
- Trade result handling with P&L updates
- Daily reset

## Modified Files

### `cfd_engine/include/CfdEngine.hpp`
- Added includes for new engine headers
- Integrated engine processing in `processTick()`:
  - New engines take priority for INDEX and GOLD symbols
  - FX falls back to existing PureScalper
- Added engine integration initialization in `start()`

## Architecture

```
PortfolioModeController
    │
    ├── INDEX_PRIORITY mode
    │   ├── IndexImpulseEngine (E1 + E2)
    │   │   ├── NAS100
    │   │   ├── US30
    │   │   └── SPX500
    │   └── PureScalper (FX)
    │
    └── GOLD_PRIORITY mode
        ├── GoldCampaignEngine (G1 + G2)
        │   └── XAUUSD
        └── IndexImpulseEngine (reduced risk)
            ├── NAS100
            ├── US30
            └── SPX500
```

## Expected Performance (From Backtests)

| Metric | Index Engine | Gold Engine |
|--------|-------------|-------------|
| Win rate | 30-45% | 30-45% |
| Avg win | 2.5-5R | 3-6R |
| Avg loss | 1R | 1R |
| Trades/week | 2-4 per symbol | 2-5 total |
| Profit factor | >1.6 | >1.6 |

## Key Principles

1. **Trailing exits only** - No TP, no time exits. Let winners run.
2. **Structural filtering** - Remove bad trades, not optimize parameters.
3. **Capital hierarchy** - One priority mode per day.
4. **No correlation bleed** - Indices and gold don't compete.
5. **Campaign vs Event** - Gold = campaigns, Indices = events.

## Build Notes

Include paths added:
```
/include/quality/
/include/engines/
/include/portfolio/
/include/integration/
```

No new dependencies. Uses existing infrastructure.

## Next Steps

1. Shadow test for 2 weeks minimum
2. Compare shadow results to backtest expectations
3. Tune parameters if needed based on live market structure
4. Gradual promotion to live after proven expectancy

## Related Documents

- `chimera_index_impulse.md` - Index engine specification
- `chimera_gold_campaign.md` - Gold engine specification
- `chimera_merged_portfolio.md` - Portfolio architecture
- `chimera_codecut.md` - Market quality cuts specification
