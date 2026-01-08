# Chimera v4.12.1 Changelog

## Gold Quad Engine Integration

### New Files
- `cfd_engine/include/gold_quad_engine.hpp` - Complete Gold microstructure trading engine
- `gold/gold_backtest_fast.py` - Python backtester with WFA + Monte Carlo
- `docs/GOLD_INTEGRATION.md` - Integration guide

### Gold Engine Features

#### Four-Engine Architecture
1. **MR (Mean Revert)** - High-frequency liquidity harvesting
   - Weight: 0.75x
   - TP/SL: $0.30 / $0.18
   - Operates in INV_CORR state

2. **SF (Stop Fade)** - Stop hunt reversion
   - Weight: 1.25x
   - TP/SL: $0.70 / $0.35
   - Daily cap: 3 trades

3. **SRM (Sweep Repricing Momentum)** - Micro momentum (LOCKED)
   - Weight: 2.25x
   - TP/SL: $2.00 / $0.70
   - Entry-time size scaling: 1.0-1.8x
   - Daily cap: 2 trades
   - Daily loss cap: $1,200

4. **GRI (Gold Regime Ignition)** - Macro momentum
   - Weight: 3.00x
   - Macro sweep threshold: $1.50 in 600ms
   - 95th percentile velocity filter
   - Partial TP + runner with trailing
   - Daily cap: 1 trade

#### Risk Controls
- Global daily loss cap: $2,500
- SRM-specific daily stop: $1,200
- Session filtering for all engines
- State-based trade gating

#### Validation Results
- Combined PnL: ~$45,000
- 99th percentile DD: ~$11,700
- WFA: 11/15 windows positive for SRM
- Clean diversification between engines

### Technical Notes
- Header-only C++ implementation
- No external dependencies beyond STL
- Numba-accelerated Python backtester
- Full Monte Carlo tail analysis
