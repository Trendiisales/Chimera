// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceConfig.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Binance API configuration - keys, endpoints, symbols
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.13 CHANGES:
//   - SWITCHED TO LIVE BINANCE (stream.binance.com)
//   - Added SHADOW mode trade blocking (two-layer safety)
//   - Added @bookTicker stream for real-time price updates
//   - API key with TRADING DISABLED on Binance side (safety layer 2)
//
// ⚠️  TRADE MODE: SHADOW - NO ORDERS WILL BE SENT
// ⚠️  API KEY: Trading disabled on Binance side (backup safety)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <iostream>
#include "../SymbolId.hpp"

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// TRADE MODE - CRITICAL SAFETY SYSTEM (v3.0)
// ─────────────────────────────────────────────────────────────────────────────
// THREE-LAYER SAFETY:
// 1. Compile-time: ACTIVE_TRADE_MODE constant
// 2. Runtime: config.paper_mode flag
// 3. API-level: Binance trading permissions OFF
// ─────────────────────────────────────────────────────────────────────────────
enum class TradeMode : uint8_t {
    SHADOW = 0,   // Signals fire, NO orders sent, NO paper PnL
    PAPER  = 1,   // Simulated fills using ShadowExecutor, track paper PnL
    LIVE   = 2    // REAL MONEY - requires ALL safety checks passed
};

// ⚠️ PAPER MODE: Live data, simulated fills, track PnL
// Change to LIVE only after:
// 1. Positive expectancy proven in PAPER mode
// 2. Expectancy slope >= 0 for 2+ sessions
// 3. No regime bleed during quiet periods
// 4. Explicit unlock code entered
constexpr TradeMode ACTIVE_TRADE_MODE = TradeMode::PAPER;

// Helper to check if REAL orders should be sent
[[nodiscard]] inline constexpr bool is_live_trading_enabled() noexcept {
    return ACTIVE_TRADE_MODE == TradeMode::LIVE;
}

// Helper to check if paper trading is active
[[nodiscard]] inline constexpr bool is_paper_mode() noexcept {
    return ACTIVE_TRADE_MODE == TradeMode::PAPER;
}

// Helper to check if any trading logic should run (paper or live)
[[nodiscard]] inline constexpr bool is_trading_logic_enabled() noexcept {
    return ACTIVE_TRADE_MODE != TradeMode::SHADOW;
}

// Get trade mode string for logging
[[nodiscard]] inline const char* trade_mode_str() noexcept {
    switch (ACTIVE_TRADE_MODE) {
        case TradeMode::SHADOW: return "SHADOW";
        case TradeMode::PAPER:  return "PAPER";
        case TradeMode::LIVE:   return "LIVE";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Environment Selection
// ─────────────────────────────────────────────────────────────────────────────
enum class Environment : uint8_t {
    TESTNET    = 0,
    PRODUCTION = 1
};

// v3.0: PRODUCTION (LIVE DATA) with PAPER execution
constexpr Environment ACTIVE_ENV = Environment::PRODUCTION;

// ─────────────────────────────────────────────────────────────────────────────
// API Credentials (TESTNET - kept for reference)
// ─────────────────────────────────────────────────────────────────────────────
namespace Testnet {
    constexpr const char* API_KEY = 
        "Mn9pRzsRbbMwMtVoo6uYul8kega7g1UbUfdmcpg1B6aTcJ7jfosAnRa6i0t4FkTk";
    constexpr const char* SECRET_KEY = 
        "1szbPpeJv0Veb0oBFh9ka3frLERLyvSH2gud1dxwVT46r98JTrJeCqv8fdPMbtzc";
    
    constexpr const char* WS_STREAM_HOST = "stream.testnet.binance.vision";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    constexpr const char* WS_API_HOST = "ws-api.testnet.binance.vision";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    constexpr const char* REST_HOST = "testnet.binance.vision";
    constexpr uint16_t    REST_PORT = 443;
}

// ─────────────────────────────────────────────────────────────────────────────
// Production Credentials - LIVE BINANCE
// ─────────────────────────────────────────────────────────────────────────────
namespace Production {
    // v7.13: Jo's API key - TRADING DISABLED on Binance side
    // IP restricted to: 45.85.3.38
    // Permissions: Read ONLY (Spot Trading DISABLED)
    constexpr const char* API_KEY = 
        "J3jwWRSWCPLN4N4vfav0gSze6bWVs6eruZzzAfhmU86u1nDk0ESXEisgzsQujndH";
    constexpr const char* SECRET_KEY = 
        "7QAqP69rjcBhFEwJq8IWPs4DVVRHCcEuSPkJJ4O4tlIYlVMlE4eNtdQnBmWtt0Nu";
    
    // WebSocket endpoints - LIVE
    constexpr const char* WS_STREAM_HOST = "stream.binance.com";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    // WebSocket API (for orders) - LIVE
    constexpr const char* WS_API_HOST = "ws-api.binance.com";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    // REST - LIVE
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
    TradeMode   trade_mode;
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
            true,
            ACTIVE_TRADE_MODE
        };
    } else {
        return {
            Production::API_KEY,
            Production::SECRET_KEY,
            Production::WS_STREAM_HOST,
            Production::WS_STREAM_PATH,
            Production::WS_STREAM_PORT,
            Production::WS_API_HOST,
            Production::WS_API_PATH,
            Production::WS_API_PORT,
            Production::REST_HOST,
            Production::REST_PORT,
            false,
            ACTIVE_TRADE_MODE
        };
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Startup Safety Check - MUST be called at engine start
// ─────────────────────────────────────────────────────────────────────────────
inline void print_trade_mode_banner() noexcept {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    if (ACTIVE_TRADE_MODE == TradeMode::SHADOW) {
        std::cout << "║  🔒 SHADOW MODE - LIVE DATA, NO TRADING                      ║\n";
        std::cout << "║  Orders will be BLOCKED at engine level                      ║\n";
        std::cout << "║  API key has trading DISABLED on Binance side                ║\n";
    } else if (ACTIVE_TRADE_MODE == TradeMode::PAPER) {
        std::cout << "║  📝 PAPER MODE - SIMULATED TRADING                           ║\n";
        std::cout << "║  Paper fills only, no real orders                            ║\n";
    } else {
        std::cout << "║  ⚠️  LIVE TRADING MODE - REAL MONEY AT RISK                  ║\n";
        std::cout << "║  Orders WILL be sent to exchange!                            ║\n";
    }
    std::cout << "║                                                                ║\n";
    std::cout << "║  Environment: " << (ACTIVE_ENV == Environment::PRODUCTION ? "PRODUCTION" : "TESTNET  ") << "                                    ║\n";
    std::cout << "║  Trade Mode:  " << trade_mode_str() << "                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
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
// v4.2.2: Added thin-liquid alts (worse microstructure = more opportunities)
constexpr std::array<SymbolConfig, 7> SYMBOLS = {{
    {
        BinanceSymbols::BTCUSDT,
        "BTCUSDT",
        "btcusdt",
        0.01,       // tick_size
        0.00001,    // lot_size
        10.0,       // min_notional
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
    },
    // v4.2.2: Thin-liquid alts - wider spreads, more edge opportunities
    {
        BinanceSymbols::AVAXUSDT,
        "AVAXUSDT",
        "avaxusdt",
        0.01,
        0.01,
        10.0,
        2,
        2
    },
    {
        BinanceSymbols::LINKUSDT,
        "LINKUSDT",
        "linkusdt",
        0.001,
        0.01,
        10.0,
        3,
        2
    },
    {
        BinanceSymbols::OPUSDT,
        "OPUSDT",
        "opusdt",
        0.001,
        0.1,
        10.0,
        3,
        1
    },
    {
        BinanceSymbols::ARBUSDT,
        "ARBUSDT",
        "arbusdt",
        0.0001,
        0.1,
        10.0,
        4,
        1
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
// v7.13: Added @bookTicker for REAL-TIME best bid/ask updates
// This is the FASTEST stream - fires on EVERY price change

inline void build_depth_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@depth20@100ms", sym.stream_lower);
}

inline void build_trade_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@trade", sym.stream_lower);
}

// Build combined stream path for all symbols
// v7.13: Format includes @bookTicker for fastest price updates
// /stream?streams=btcusdt@bookTicker/btcusdt@depth20@100ms/btcusdt@trade/...
inline void build_combined_stream_path(char* buf, size_t buf_size) noexcept {
    int offset = snprintf(buf, buf_size, "/stream?streams=");
    
    bool first = true;
    for (const auto& sym : SYMBOLS) {
        if (!first) {
            offset += snprintf(buf + offset, buf_size - offset, "/");
        }
        first = false;
        
        // v7.13: @bookTicker gives REAL-TIME best bid/ask on EVERY change
        // This is the fastest stream - critical for HFT!
        offset += snprintf(buf + offset, buf_size - offset, "%s@bookTicker", sym.stream_lower);
        
        // @depth20@100ms gives FULL top-20 book every 100ms (for depth analysis)
        offset += snprintf(buf + offset, buf_size - offset, "/%s@depth20@100ms", sym.stream_lower);
        
        // Trade stream for volume/trade flow analysis
        offset += snprintf(buf + offset, buf_size - offset, "/%s@trade", sym.stream_lower);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Trading Parameters
// ─────────────────────────────────────────────────────────────────────────────
namespace TradingParams {
    // Position sizing
    constexpr double MAX_POSITION_BTC  = 0.01;
    constexpr double MAX_POSITION_ETH  = 0.1;
    constexpr double MAX_POSITION_SOL  = 1.0;
    
    // Risk limits
    constexpr double DAILY_LOSS_LIMIT_USD = 100.0;
    constexpr double MAX_SPREAD_BPS       = 15.0;
    constexpr double MAX_SLIPPAGE_BPS     = 10.0;
    
    // Rate limiting
    constexpr uint64_t ORDER_COOLDOWN_NS  = 100'000'000;  // 100ms between orders
    constexpr uint32_t MAX_ORDERS_PER_SYMBOL = 5;
    
    // Latency thresholds
    constexpr uint64_t STALE_TICK_NS      = 2'000'000'000; // 2 seconds
    constexpr uint64_t MAX_ORDER_LATENCY_NS = 500'000'000; // 500ms order timeout
}

} // namespace Binance
} // namespace Chimera
