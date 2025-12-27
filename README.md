# CHIMERA v6 - Dual Engine HFT System

## Architecture

Two completely independent trading engines sharing ONLY atomic state:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        SHARED ATOMICS ONLY                               │
│   GlobalKill (atomic<bool>) + DailyLossGuard (atomic<double>)           │
├──────────────────────────────────┬──────────────────────────────────────┤
│       BINANCE ENGINE (CPU 1)     │       CTRADER ENGINE (CPU 2)         │
│                                  │                                      │
│   Protocol: WebSocket            │   Protocol: FIX 4.4 over SSL         │
│   Host: stream.binance.com:9443  │   Host: demo-uk-eqx-01.p.c-trader.com│
│   Format: JSON                   │   Ports: 5211 (quote), 5212 (trade)  │
│                                  │                                      │
│   Symbols:                       │   Symbols:                           │
│   - BTCUSDT                      │   - EURUSD, GBPUSD, USDJPY, AUDUSD   │
│   - ETHUSDT                      │   - XAUUSD, XAGUSD                   │
│   - SOLUSDT                      │   - US30, US100                      │
└──────────────────────────────────┴──────────────────────────────────────┘
```

## Build Instructions

### Linux (Ubuntu/Debian)
```bash
# Install dependencies
sudo apt-get install cmake build-essential libssl-dev

# Build
mkdir build && cd build
cmake ..
make

# Run
./chimera
```

### Windows Cross-Compile (from Mac with Zig)
```bash
# Build Windows .exe on Mac
zig cc -target x86_64-windows-gnu ...
```

## Binaries

| Binary | Description |
|--------|-------------|
| `chimera` | Main dual-engine trading system |
| `crypto_test` | Binance engine compilation test |
| `cfd_test` | cTrader FIX engine compilation test |

## Configuration

### Binance (Crypto)
- Configured in `crypto_engine/include/binance/BinanceConfig.hpp`
- Uses WebSocket API for orders (NOT REST - preserves 0.2ms latency)

### cTrader (CFD)
- Configured in `cfd_engine/include/fix/FIXConfig.hpp`
- BlackBull Markets demo account pre-configured
- FIX 4.4 protocol over SSL

## Risk Management

- **Daily Loss Limit**: -$500 NZD (configurable)
- **GlobalKill**: Emergency stop across both engines
- **Per-Symbol Limits**: Position size, order rate, cooldowns

## Project Structure

```
Chimera_v6/
├── CMakeLists.txt
├── include/shared/          # Atomic shared state
│   ├── GlobalKill.hpp
│   ├── DailyLossGuard.hpp
│   └── Venue.hpp
├── crypto_engine/           # Binance WebSocket engine
│   └── include/
│       ├── binance/         # WebSocket, Parser, OrderSender
│       ├── micro/           # CRTP micro engines
│       ├── signal/          # SignalAggregator
│       ├── regime/          # RegimeClassifier
│       └── strategy/        # MultiStrategyCoordinator
├── cfd_engine/              # cTrader FIX engine
│   └── include/
│       ├── fix/             # FIX 4.4 SSL stack
│       ├── micro/           # 17 CRTP micro engines
│       ├── strategy/        # 10-bucket voting system
│       ├── risk/            # RiskGuardian
│       └── execution/       # SmartExecutionEngine
└── src/
    ├── main_dual.cpp        # Main entry point
    ├── crypto_test.cpp      # Binance test
    └── cfd_test.cpp         # cTrader test
```

## Version History

- **v6.0** (2024-12-21): Dual engine integration complete
  - Both engines compile and link
  - Full FIX 4.4 SSL stack for cTrader
  - 10-bucket strategy voting system
  - Atomic-only cross-engine communication

## Author

Jo - Quantitative Trader & HFT Developer
