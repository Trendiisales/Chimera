# CHIMERA v6.97 CHANGELOG

## Release Date: 2024-12-24

### 🔴 CRITICAL FIXES

#### 1. Binance API Keys & Endpoints (BinanceConfig.hpp)
- **FIXED**: API keys now use REAL testnet.binance.vision credentials
- **FIXED**: WS_API_HOST changed from `stream.testnet.binance.vision` to `ws-api.testnet.binance.vision`
- **FIXED**: Stream subscription changed from `@depth@100ms` (diffs) to `@depth20@100ms` (full snapshots)
  - This fixes the "empty order book" problem where bid=0, ask=0
  - Full snapshots don't require REST API seed

#### 2. Binance PnL Tracking (BinanceEngine.hpp)
- **FIXED**: PnL calculation was always 0.0 (TODO comment in code)
- **NEW**: Added `PositionTracker` class to track entry prices per symbol
- **NEW**: Calculates realized PnL on position close: `(exit - entry) * qty`
- **NEW**: Tracks wins/losses counts for performance metrics
- **NEW**: PnL now properly updates DailyLossGuard

#### 3. CFD Symbol Filtering (CfdEngine.hpp)
- **FIXED**: Symbol enable/disable from dashboard was ignored
- **NEW**: `processTick()` now checks `TradingConfig::SymbolConfig::enabled`
- **NEW**: Disabled symbols are completely skipped (no processing overhead)

#### 4. CFD PnL Calculation (main_dual.cpp)
- **FIXED**: 1:1 bps-to-NZD mapping was incorrect (could be 2-3x wrong)
- **NEW**: Proper conversion based on:
  - Contract size per instrument (Gold=100oz, Forex=100k, etc.)
  - Position value at current price
  - USD to NZD exchange rate (~1.65)

#### 5. Latency Tracking (BinanceEngine.hpp)
- **FIXED**: Latency showed 0μs because it wasn't being updated per-message
- **NEW**: Added `current_latency_ms_` atomic for real-time latency display
- **NEW**: Latency now logged in depth message debug output

### 🟡 MINOR FIXES

- **REMOVED**: Duplicate `setIndicesSymbols()` call in main_dual.cpp
- **UPDATED**: Version strings to v6.97 throughout codebase
- **UPDATED**: Banner now shows "TESTNET" for Binance status

### 📋 API CREDENTIALS

```cpp
// testnet.binance.vision (generated 2024-12-24)
API_KEY: Mn9pRzsRbbMwMtVoo6uYul8kega7g1UbUfdmcpg1B6aTcJ7jfosAnRa6i0t4FkTk
SECRET:  1szbPpeJv0Veb0oBFh9ka3frLERLyvSH2gud1dxwVT46r98JTrJeCqv8fdPMbtzc
```

### 📁 FILES MODIFIED

1. `crypto_engine/include/binance/BinanceConfig.hpp` - API keys, endpoints, streams
2. `crypto_engine/include/binance/BinanceEngine.hpp` - PnL tracking, latency
3. `cfd_engine/include/CfdEngine.hpp` - Symbol filtering
4. `src/main_dual.cpp` - Version, PnL calculation, cleanup

### 🚀 DEPLOY COMMAND

```bash
cp /mnt/c/Chimera/Chimera_v6.97.zip ~/
cd ~ && unzip -o Chimera_v6.97.zip
cd ~/Chimera_v6.97/build && cmake .. && make -j4
cd ~/Chimera_v6.97 && ./build/chimera
```

### ⚠️ NOTES

- VPS: 45.85.3.38 (WSL on Windows)
- Dashboard: http://45.85.3.38:8080/
- WebSocket: ws://45.85.3.38:7777
- Daily Loss Limit: -$500 NZD
- Binance Mode: TESTNET (virtual funds only)
