# Chimera HFT Trading System

Professional-grade high-frequency trading system with institutional telemetry.

## Features

- **10 Trading Strategies**: ETH_FADE, SOL_FADE, BTC_CASCADE, ETH_SNIPER, MEAN_REVERSION, and more
- **Multi-Layer Risk Management**: Position gates, drift detection, PnL governors, kill switches
- **Pro-Grade Telemetry**: Execution quality tracking, edge attribution, regime detection
- **Shadow Mode**: Test strategies with real market data without executing trades
- **Live Trading**: Full Binance Spot integration

## Quick Start
```bash
# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run (shadow mode)
./chimera

# Dashboard
open http://localhost:8080
```

## Architecture

- **Core**: Event-driven architecture with lock-free data structures
- **Strategies**: Independent strategy engines with desk-level risk limits  
- **Execution**: Multi-tier routing with position gates, adverse selection detection
- **Risk**: GlobalRiskGovernor, PnLGovernor, LatencyGovernor, UnwindCoordinator
- **Telemetry**: Real-time execution quality metrics, professional dashboard

## Dashboard Metrics

- Portfolio PnL (realized, unrealized, total)
- Per-strategy execution quality (edge, fees, slippage, latency)
- Position tracking with avg entry prices
- Win rate, profit factor, maker/taker ratios
- Capital efficiency metrics

## Configuration

Edit `.env` for live trading:
```
BINANCE_API_KEY=your_key
BINANCE_API_SECRET=your_secret
```

## Version

V22 - Professional Telemetry Edition

## License

Proprietary
