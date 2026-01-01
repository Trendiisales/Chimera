# Changelog

## [6.99] - 2024-12-24

### CRITICAL BUG FIXES

#### Binance Crypto Data Not Displaying
- **ROOT CAUSE**: `@depth20@100ms` (Partial Book Depth) uses a DIFFERENT JSON format than `@depth@100ms` (Diff Depth)!
  - Partial Book: `{"lastUpdateId":160,"bids":[...],"asks":[...]}` - NO "e" field!
  - Diff Depth: `{"e":"depthUpdate","E":123,"s":"BTCUSDT",...}` - HAS "e" field
- **FIX**: BinanceParser.hpp now detects partial book by:
  1. Checking stream name for `@depth` followed by digit (5/10/20)
  2. Looking for `lastUpdateId` field instead of `"e":"depthUpdate"`
  3. Extracting symbol from stream name (converted to uppercase)

#### CFD PnL Showing Wrong Values
- **ROOT CAUSE**: PureScalper.realized_pnl was in BPS (basis points), not currency
  - GUI displayed bps values as if they were dollars
- **FIX**: 
  - Added `realized_pnl_bps` field for thresholds/debug
  - `realized_pnl` now contains proper currency: `(exit-entry) * side * size * contract_size`
  - CfdEngine passes contract_size to scalper before each process() call

#### Crypto Latency Not Updating
- **ROOT CAUSE**: Partial book depth has no event_time field
- **FIX**: Only record latency from messages that have event_time > 0

### Files Changed
- `crypto_engine/include/binance/BinanceParser.hpp` - Detect partial book depth format
- `crypto_engine/include/binance/BinanceEngine.hpp` - Handle missing event_time
- `cfd_engine/include/strategy/PureScalper.hpp` - Currency PnL calculation
- `cfd_engine/include/CfdEngine.hpp` - Pass contract_size to scalper

## [6.98] - 2024-12-24

### CFD Trading Logic Fixes
- Mean reversion threshold: 0.2% -> 0.5% (less aggressive)
- TP/SL ratio: 15/20 -> 30/15 bps (better R:R)
- Max spread filter: 20 -> 10 bps
- Warmup: 15 -> 30 ticks

### Binance WebSocket Path Fix  
- Added missing `/stream` prefix to WebSocket path

## [6.0.0] - 2024-12-21

### Added
- **Dual Engine Architecture**: Complete integration of Binance and cTrader engines
- **main_dual.cpp**: Unified entry point managing both engines
- **CMakeLists.txt**: Updated to include both engine paths

### Binance Engine (crypto_engine/)
- WebSocket market data feed (depth + trades)
- WebSocket order sender (NOT REST - preserves latency)
- BinanceParser with zero-copy JSON parsing
- SymbolThread per-symbol isolation
- 12 CRTP micro strategies
- SignalAggregator + RegimeClassifier
- MultiStrategyCoordinator

### cTrader Engine (cfd_engine/)
- FIX 4.4 SSL transport layer
- Dual session support (QUOTE port 5211, TRADE port 5212)
- CTraderFIXClient with market data + execution
- 17 CRTP micro engines
- 10-bucket strategy voting system
- RiskGuardian with atomic checks
- SmartExecutionEngine with TWAP/VWAP

### Shared (include/shared/)
- GlobalKill: Atomic emergency stop
- DailyLossGuard: Combined PnL tracking with -$500 NZD limit
- Venue: Exchange enumeration

### Build System
- CMake 3.16+ required
- C++20 standard
- OpenSSL for SSL/TLS
- Produces 3 binaries: chimera, crypto_test, cfd_test

## [5.x] - Previous Versions
- See git history for earlier development
