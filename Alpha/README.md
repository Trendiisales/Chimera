# Alpha Trading System v1.0.0

**Codename:** APEX  
**Philosophy:** "Trade less, win more. Concentration beats diversification."

---

## Overview

Alpha is a focused, dual-engine CFD trading system that trades **ONLY** two instruments:

| Instrument | Why |
|------------|-----|
| **XAUUSD** | Best spread/volatility ratio, clear session patterns, responds predictably to USD news |
| **NAS100** | Clean momentum during US session, predictable open/close patterns |

**Everything else is ignored.** Not diversified—*focused*.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      ALPHA ENGINE                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌─────────────────┐         ┌─────────────────┐              │
│   │  XAUUSD Engine  │         │  NAS100 Engine  │              │
│   │  (isolated)     │         │  (isolated)     │              │
│   └────────┬────────┘         └────────┬────────┘              │
│            │                           │                        │
│            └───────────┬───────────────┘                        │
│                        │                                        │
│              ┌─────────▼─────────┐                              │
│              │  CTrader FIX      │                              │
│              │  (shared client)  │                              │
│              └───────────────────┘                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

Each instrument has its own **InstrumentEngine** with:
- Independent microstructure analysis
- Independent signal generation  
- Independent position management
- Independent expectancy tracking

They share ONE FIX connection to cTrader.

---

## Peak Trading Sessions

### XAUUSD (Gold)

| Session | UTC Time | Size Multiplier | Description |
|---------|----------|-----------------|-------------|
| 🔥 **London Open** | 07:00-09:00 | 2.0x | Physical gold market opens |
| 🔥 **US Data** | 13:30-15:00 | 2.5x | Economic data / FOMC |
| ⚠️ Asia | 00:00-03:00 | 0.7x | Asian physical demand |
| ⚠️ London PM | 10:00-12:00 | 0.8x | Afternoon liquidity |

### NAS100 (Nasdaq)

| Session | UTC Time | Size Multiplier | Description |
|---------|----------|-----------------|-------------|
| 🔥 **Cash Open** | 13:30-15:30 | 2.5x | Gap fills, momentum ignition |
| 🔥 **Power Hour** | 19:00-20:30 | 2.0x | End of day positioning |
| ⚠️ Pre-Market | 12:00-13:30 | 0.6x | Lower liquidity |
| ⚠️ Midday | 15:30-18:00 | 0.4x | Lunch chop (careful!) |

**OFF = No trading.** Outside peak/secondary windows, Alpha does nothing.

---

## Risk Management

### Per-Trade Limits
- **Base risk:** 0.5% of equity
- **Peak session risk:** Up to 1.2%
- **Maximum risk:** 1.5%

### Daily Limits
- **Daily loss limit:** 2R
- **Max daily trades:** 20
- **Consecutive loss limit:** 5

### Position Exits
- **Take Profit:** XAUUSD=15bps, NAS100=12bps
- **Stop Loss:** XAUUSD=5bps, NAS100=4bps
- **Trailing Stop:** Activates at 6-8 bps profit
- **Time Exit:** 30-45 seconds max hold

---

## Installation

### Prerequisites
- Linux (native or WSL2 on Windows)
- G++ with C++17 support
- OpenSSL development headers
- cTrader FIX API credentials (BlackBull Markets)

### Build

```bash
# Clone/extract to ~/Alpha
cd ~/Alpha

# Build
make

# Or with cmake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Configuration

1. Copy `config/config.ini.template` to `config.ini`
2. Fill in your cTrader FIX credentials:
   - `sender_comp_id` - Your FIX SenderCompID
   - `username` - Your FIX username
   - `password` - Your FIX password

---

## Usage

```bash
# Shadow mode (paper trading) - DEFAULT
./alpha

# Live mode (real orders)
./alpha --live

# Custom equity
./alpha --equity 50000

# Show help
./alpha --help
```

---

## Files

```
Alpha/
├── CMakeLists.txt              # CMake build
├── Makefile                    # Direct make build
├── README.md                   # This file
├── config/
│   └── config.ini.template     # Configuration template
├── include/
│   ├── AlphaEngine.hpp         # Main orchestrator
│   ├── core/
│   │   └── Types.hpp           # Core types
│   ├── engine/
│   │   └── InstrumentEngine.hpp # Per-instrument logic
│   ├── fix/                    # FIX 4.4 implementation
│   │   ├── CTraderFIXClient.hpp
│   │   ├── FIXConfig.hpp
│   │   ├── FIXMessage.hpp
│   │   ├── FIXSession.hpp
│   │   └── FIXSSLTransport.hpp
│   └── session/
│       └── SessionDetector.hpp  # Session detection
└── src/
    └── main.cpp                 # Entry point
```

---

## Signal Logic

### TRENDING Regime
- Both fast and slow momentum agree on direction
- Enter in direction of momentum
- Higher conviction = larger size

### RANGING Regime
- Fade extremes (overextensions)
- Wait for momentum > 1.5 bps then fade
- Only for XAUUSD (NAS100 skips ranging)

### VOLATILE Regime
- Only take strong momentum bursts (> 2.5 bps)
- Reduced size (0.6x multiplier)
- Only for XAUUSD (NAS100 skips volatile)

### QUIET/TRANSITION
- No trading
- Wait for regime to clarify

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Win Rate | 35-45% |
| Risk:Reward | 3:1 minimum |
| Daily Edge | +10-20 bps |
| Trades/Day | 3-8 |

### Expected Returns (Conservative)

| Account | Daily | Monthly |
|---------|-------|---------|
| $10,000 | +$15-20 | +$300-400 |
| $50,000 | +$75-100 | +$1,500-2,000 |

---

## Differences from Chimera

| Aspect | Chimera | Alpha |
|--------|---------|-------|
| Lines of code | 43,500 | ~5,000 |
| Instruments | 10+ | 2 |
| Engines | 2 (crypto + CFD) | 2 (XAUUSD + NAS100) |
| Complexity | High | Minimal |
| Focus | Coverage | Conviction |

**Alpha is what Chimera's CFD engine should have been.**

---

## License

Proprietary. For personal use only.

---

**Built with discipline. Traded with conviction. Profits with patience.**
