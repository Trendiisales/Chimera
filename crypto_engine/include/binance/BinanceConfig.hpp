// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceConfig.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Binance API configuration - reads credentials from config.ini
// OWNER: Jo
// LAST VERIFIED: 2025-01-04
//
// v4.9.32 CHANGES:
//   - REMOVED HARDCODED CREDENTIALS - now reads from config.ini
//   - API key/secret loaded at runtime via ConfigLoader
//   - Endpoints still constexpr (they don't change)
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <string>
#include <iostream>
#include <cstring>
#include "../SymbolId.hpp"
#include "../../../include/shared/ConfigLoader.hpp"

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// TRADE MODE - CRITICAL SAFETY SYSTEM (v3.0)
// ─────────────────────────────────────────────────────────────────────────────
enum class TradeMode : uint8_t {
    SHADOW = 0,   // Signals fire, NO orders sent, NO paper PnL
    PAPER  = 1,   // Simulated fills using ShadowExecutor, track paper PnL
    LIVE   = 2    // REAL MONEY - requires ALL safety checks passed
};

// ⚠️ LIVE MODE: Real money trading enabled
constexpr TradeMode ACTIVE_TRADE_MODE = TradeMode::LIVE;

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

constexpr Environment ACTIVE_ENV = Environment::PRODUCTION;

// ─────────────────────────────────────────────────────────────────────────────
// Endpoint Configuration (constexpr - these don't change)
// ─────────────────────────────────────────────────────────────────────────────
namespace Testnet {
    constexpr const char* WS_STREAM_HOST = "stream.testnet.binance.vision";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    constexpr const char* WS_API_HOST = "ws-api.testnet.binance.vision";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    constexpr const char* REST_HOST = "testnet.binance.vision";
    constexpr uint16_t    REST_PORT = 443;
}

namespace Production {
    constexpr const char* WS_STREAM_HOST = "stream.binance.com";
    constexpr const char* WS_STREAM_PATH = "/stream";
    constexpr uint16_t    WS_STREAM_PORT = 9443;
    
    constexpr const char* WS_API_HOST = "ws-api.binance.com";
    constexpr const char* WS_API_PATH = "/ws-api/v3";
    constexpr uint16_t    WS_API_PORT = 443;
    
    constexpr const char* REST_HOST = "api.binance.com";
    constexpr uint16_t    REST_PORT = 443;
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Credentials - LOADED FROM config.ini
// ─────────────────────────────────────────────────────────────────────────────
class RuntimeCredentials {
public:
    static RuntimeCredentials& instance() {
        static RuntimeCredentials inst;
        return inst;
    }
    
    bool load() {
        auto& cfg = ConfigLoader::instance();
        if (!cfg.load()) {
            std::cerr << "[BinanceConfig] FATAL: Failed to load config.ini!\n";
            return false;
        }
        
        api_key_ = cfg.get("binance", "api_key", "");
        api_secret_ = cfg.get("binance", "api_secret", "");
        
        if (api_key_.empty() || api_secret_.empty()) {
            std::cerr << "[BinanceConfig] FATAL: Missing api_key or api_secret in config.ini!\n";
            std::cerr << "[BinanceConfig] Expected [binance] section with api_key and api_secret\n";
            return false;
        }
        
        // Copy to fixed buffers for const char* access
        strncpy(api_key_buf_, api_key_.c_str(), sizeof(api_key_buf_) - 1);
        api_key_buf_[sizeof(api_key_buf_) - 1] = '\0';
        
        strncpy(api_secret_buf_, api_secret_.c_str(), sizeof(api_secret_buf_) - 1);
        api_secret_buf_[sizeof(api_secret_buf_) - 1] = '\0';
        
        loaded_ = true;
        
        std::cout << "[BinanceConfig] ✓ Credentials loaded from config.ini\n";
        std::cout << "[BinanceConfig]   API Key: " << api_key_.substr(0, 8) << "..." 
                  << api_key_.substr(api_key_.length() - 4) << "\n";
        
        return true;
    }
    
    const char* api_key() const { 
        if (!loaded_) {
            std::cerr << "[BinanceConfig] ERROR: Credentials not loaded! Call load() first.\n";
        }
        return api_key_buf_; 
    }
    
    const char* api_secret() const { 
        if (!loaded_) {
            std::cerr << "[BinanceConfig] ERROR: Credentials not loaded! Call load() first.\n";
        }
        return api_secret_buf_; 
    }
    
    bool is_loaded() const { return loaded_; }

private:
    RuntimeCredentials() : loaded_(false) {
        api_key_buf_[0] = '\0';
        api_secret_buf_[0] = '\0';
    }
    
    std::string api_key_;
    std::string api_secret_;
    char api_key_buf_[128];
    char api_secret_buf_[128];
    bool loaded_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Active Configuration
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

// Get config - MUST call RuntimeCredentials::instance().load() first!
[[nodiscard]] inline Config get_config() noexcept {
    auto& creds = RuntimeCredentials::instance();
    
    if constexpr (ACTIVE_ENV == Environment::TESTNET) {
        return {
            creds.api_key(),
            creds.api_secret(),
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
            creds.api_key(),
            creds.api_secret(),
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
inline bool initialize_binance_config() {
    if (!RuntimeCredentials::instance().load()) {
        std::cerr << "[BinanceConfig] FATAL: Cannot start without valid credentials!\n";
        return false;
    }
    return true;
}

inline void print_trade_mode_banner() noexcept {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    if (ACTIVE_TRADE_MODE == TradeMode::SHADOW) {
        std::cout << "║  🔒 SHADOW MODE - LIVE DATA, NO TRADING                      ║\n";
        std::cout << "║  Orders will be BLOCKED at engine level                      ║\n";
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
    std::cout << "║  Credentials: FROM config.ini                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolConfig {
    SymbolId    id;
    const char* symbol;
    const char* stream_lower;
    double      tick_size;
    double      lot_size;
    double      min_notional;
    int         price_precision;
    int         qty_precision;
};

constexpr std::array<SymbolConfig, 7> SYMBOLS = {{
    {
        BinanceSymbols::BTCUSDT,
        "BTCUSDT",
        "btcusdt",
        0.01,
        0.00001,
        10.0,
        2,
        5
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

[[nodiscard]] inline const SymbolConfig* find_symbol(SymbolId id) noexcept {
    for (const auto& s : SYMBOLS) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

[[nodiscard]] inline const SymbolConfig* find_symbol(std::string_view name) noexcept {
    for (const auto& s : SYMBOLS) {
        if (name == s.symbol) return &s;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream Names (for WebSocket subscription)
// ─────────────────────────────────────────────────────────────────────────────
inline void build_depth_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@depth20@100ms", sym.stream_lower);
}

inline void build_trade_stream(const SymbolConfig& sym, char* buf, size_t buf_size) noexcept {
    snprintf(buf, buf_size, "%s@trade", sym.stream_lower);
}

inline void build_combined_stream_path(char* buf, size_t buf_size) noexcept {
    int offset = snprintf(buf, buf_size, "/stream?streams=");
    
    bool first = true;
    for (const auto& sym : SYMBOLS) {
        if (!first) {
            offset += snprintf(buf + offset, buf_size - offset, "/");
        }
        first = false;
        
        offset += snprintf(buf + offset, buf_size - offset, "%s@bookTicker", sym.stream_lower);
        offset += snprintf(buf + offset, buf_size - offset, "/%s@depth20@100ms", sym.stream_lower);
        offset += snprintf(buf + offset, buf_size - offset, "/%s@trade", sym.stream_lower);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Trading Parameters
// ─────────────────────────────────────────────────────────────────────────────
namespace TradingParams {
    constexpr double MAX_POSITION_BTC  = 0.01;
    constexpr double MAX_POSITION_ETH  = 0.1;
    constexpr double MAX_POSITION_SOL  = 1.0;
    
    constexpr double DAILY_LOSS_LIMIT_USD = 100.0;
    constexpr double MAX_SPREAD_BPS       = 15.0;
    constexpr double MAX_SLIPPAGE_BPS     = 10.0;
    
    constexpr uint64_t ORDER_COOLDOWN_NS  = 100'000'000;
    constexpr uint32_t MAX_ORDERS_PER_SYMBOL = 5;
    
    constexpr uint64_t STALE_TICK_NS      = 2'000'000'000;
    constexpr uint64_t MAX_ORDER_LATENCY_NS = 500'000'000;
}

} // namespace Binance
} // namespace Chimera
