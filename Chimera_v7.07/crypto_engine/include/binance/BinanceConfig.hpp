// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceConfig.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Binance API configuration - keys, endpoints, symbols
// OWNER: Jo
// LAST VERIFIED: 2024-12-24
//
// v6.97 FIXES:
//   - Updated to REAL testnet.binance.vision keys
//   - Fixed WS_API_HOST: ws-api.testnet.binance.vision (was wrong)
//   - Fixed stream subscription: @depth20@100ms (full snapshots, not diffs)
//   - This prevents empty order book problem
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include "../SymbolId.hpp"

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// Environment Selection
// ─────────────────────────────────────────────────────────────────────────────
enum class Environment : uint8_t {
    TESTNET    = 0,
    PRODUCTION = 1
};

// CHANGE THIS TO SWITCH ENVIRONMENTS
// v6.97: Using TESTNET with testnet.binance.vision keys
constexpr Environment ACTIVE_ENV = Environment::TESTNET;

// ─────────────────────────────────────────────────────────────────────────────
// API Credentials (TESTNET - safe to expose)
// ─────────────────────────────────────────────────────────────────────────────
namespace Testnet {
    // v6.97: REAL testnet.binance.vision keys (generated 2024-12-24)
    constexpr const char* API_KEY = 
        "Mn9pRzsRbbMwMtVoo6uYul8kega7g1UbUfdmcpg1B6aTcJ7jfosAnRa6i0t4FkTk";
    constexpr const char* SECRET_KEY = 
        "1szbPpeJv0Veb0oBFh9ka3frLERLyvSH2gud1dxwVT46r98JTrJeCqv8fdPMbtzc";
    
    // WebSocket STREAM endpoints (market data)
    // v6.97: stream.testnet.binance.vision for market data streams
    constexpr const char* WS_STREAM_HOST = "stream.testnet.binance.vision";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    // WebSocket API (for orders) 
    // v6.97 FIX: ws-api.testnet.binance.vision (NOT stream.testnet)
    constexpr const char* WS_API_HOST = "ws-api.testnet.binance.vision";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    // REST (only for initial depth snapshot if needed)
    constexpr const char* REST_HOST = "testnet.binance.vision";
    constexpr uint16_t    REST_PORT = 443;
}

// ─────────────────────────────────────────────────────────────────────────────
// Production Credentials (loaded from environment)
// ─────────────────────────────────────────────────────────────────────────────
namespace Production {
    // DO NOT HARDCODE PRODUCTION KEYS
    // Load from: getenv("BINANCE_API_KEY") / getenv("BINANCE_SECRET_KEY")
    
    // WebSocket endpoints
    constexpr const char* WS_STREAM_HOST = "stream.binance.com";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    // WebSocket API (for orders)
    constexpr const char* WS_API_HOST = "ws-api.binance.com";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    // REST (only for initial depth snapshot)
    constexpr const char* REST_HOST = "api.binance.com";
    constexpr uint16_t    REST_PORT = 443;
}

// ─────────────────────────────────────────────────────────────────────────────
// Active Configuration (based on ACTIVE_ENV)
// ─────────────────────────────────────────────────────────────────────────────
struct Config {
    const char* api_key;
    const char* secret_key;
    const char* ws_stream_host;
    const char* ws_stream_path;
    uint16_t    ws_stream_port;
    const char* ws_api_host;
    const char* ws_api_path;
    uint16_t    ws_api_port;
    const char* rest_host;
    uint16_t    rest_port;
    bool        is_testnet;
};

[[nodiscard]] inline constexpr Config get_config() noexcept {
    if constexpr (ACTIVE_ENV == Environment::TESTNET) {
        return {
            Testnet::API_KEY,
            Testnet::SECRET_KEY,
            Testnet::WS_STREAM_HOST,
            Testnet::WS_STREAM_PATH,
            Testnet::WS_STREAM_PORT,
            Testnet::WS_API_HOST,
            Testnet::WS_API_PATH,
            Testnet::WS_API_PORT,
            Testnet::REST_HOST,
            Testnet::REST_PORT,
            true
        };
    } else {
        return {
            nullptr,  // Load from env
            nullptr,  // Load from env
            Production::WS_STREAM_HOST,
            Production::WS_STREAM_PATH,
            Production::WS_STREAM_PORT,
            Production::WS_API_HOST,
            Production::WS_API_PATH,
            Production::WS_API_PORT,
            Production::REST_HOST,
            Production::REST_PORT,
            false
        };
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolConfig {
    SymbolId    id;
    const char* symbol;         // e.g., "BTCUSDT"
    const char* stream_lower;   // e.g., "btcusdt" (for WebSocket streams)
    double      tick_size;      // Minimum price increment
    double      lot_size;       // Minimum quantity increment
    double      min_notional;   // Minimum order value
    int         price_precision;  // Decimal places for price
    int         qty_precision;    // Decimal places for quantity
};

// Active symbols for trading
constexpr std::array<SymbolConfig, 3> SYMBOLS = {{
    {
        BinanceSymbols::BTCUSDT,
        "BTCUSDT",
        "btcusdt",
        0.01,       // tick_size
        0.00001,    // lot_size
        10.0,       // min_notional (testnet may differ)
        2,          // price_precision
        5           // qty_precision
    },
    {
        BinanceSymbols::ETHUSDT,
        "ETHUSDT",
        "ethusdt",
        0.01,
        0.0001,
        10.0,
        2,
        4
    },
    {
        BinanceSymbols::SOLUSDT,
        "SOLUSDT",
        "solusdt",
        0.01,
        0.01,
        10.0,
        2,
        2
    }
}};

constexpr size_t NUM_SYMBOLS = SYMBOLS.size();

// Find symbol config by ID
[[nodiscard]] inline const SymbolConfig* find_symbol(SymbolId id) noexcept {
    for (const auto& s : SYMBOLS) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

// Find symbol config by name
[[nodiscard]] inline const SymbolConfig* find_symbol(std::string_view name) noexcept {
    for (const auto& s : SYMBOLS) {
        if (name == s.symbol) return &s;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream Names (for WebSocket subscription)
// ─────────────────────────────────────────────────────────────────────────────
// v6.97 FIX: Use @depth20@100ms for FULL order book snapshots
// This provides top 20 levels every 100ms - NO REST snapshot needed!
// The @depth@100ms (diff depth) requires a REST snapshot seed and causes
// empty book problems when the initial snapshot is missing.

// Build depth stream name: "btcusdt@depth20@100ms"
inline void build_depth_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@depth20@100ms", sym.stream_lower);
}

// Build trade stream name: "btcusdt@trade"
inline void build_trade_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@trade", sym.stream_lower);
}

// Build combined stream path for all symbols
// v6.98 FIX: Must include /stream prefix for WebSocket path
// Format: /stream?streams=btcusdt@depth20@100ms/btcusdt@trade/ethusdt@depth20@100ms/...
inline void build_combined_stream_path(char* buf, size_t buf_size) noexcept {
    // v6.98 FIX: Was missing /stream prefix - caused connection failure!
    int offset = snprintf(buf, buf_size, "/stream?streams=");
    
    bool first = true;
    for (const auto& sym : SYMBOLS) {
        if (!first) {
            offset += snprintf(buf + offset, buf_size - offset, "/");
        }
        first = false;
        
        // v6.97 FIX: @depth20@100ms gives FULL top-20 book every 100ms
        // This is a complete snapshot - no REST API seed required!
        offset += snprintf(buf + offset, buf_size - offset, "%s@depth20@100ms", sym.stream_lower);
        
        // Add trade stream
        offset += snprintf(buf + offset, buf_size - offset, "/%s@trade", sym.stream_lower);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Trading Parameters (v6.88: relaxed for actual trading)
// ─────────────────────────────────────────────────────────────────────────────
namespace TradingParams {
    // Position sizing - v6.88: INCREASED for actual trading
    constexpr double MAX_POSITION_BTC  = 0.01;    // Max BTC position (was 0.001)
    constexpr double MAX_POSITION_ETH  = 0.1;     // Max ETH position (was 0.01)
    constexpr double MAX_POSITION_SOL  = 1.0;     // Max SOL position (was 0.1)
    
    // Risk limits - v6.88: RELAXED for bring-up
    constexpr double DAILY_LOSS_LIMIT_USD = 100.0; // Stop trading if down this much (was 50)
    constexpr double MAX_SPREAD_BPS       = 15.0;  // Don't trade if spread > 15bps (was 10)
    constexpr double MAX_SLIPPAGE_BPS     = 10.0;  // Max expected slippage (was 5)
    
    // Rate limiting - v6.88: RELAXED for crypto HFT
    constexpr uint64_t ORDER_COOLDOWN_NS  = 100'000'000;  // 100ms between orders (was 250ms)
    constexpr uint32_t MAX_ORDERS_PER_SYMBOL = 5;         // Max open orders per symbol (was 3)
    
    // Latency thresholds
    constexpr uint64_t STALE_TICK_NS      = 2'000'000'000; // 2 seconds (was 1s)
    constexpr uint64_t MAX_ORDER_LATENCY_NS = 500'000'000; // 500ms order timeout
}

} // namespace Binance
} // namespace Chimera
