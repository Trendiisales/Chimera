// ═══════════════════════════════════════════════════════════════════════════════
// include/core/SymbolId.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Symbol ID constants and compile-time mapping
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
//
// DESIGN:
// - Symbol IDs are uint16_t (0-65535)
// - Lower IDs (1-999) reserved for Binance crypto
// - Higher IDs (1000-1999) reserved for cTrader CFD
// - ID 0 is invalid/unknown
// - String lookups are cold path only
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <string_view>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Symbol ID Type
// ─────────────────────────────────────────────────────────────────────────────
using SymbolId = uint16_t;

// ─────────────────────────────────────────────────────────────────────────────
// Binance Symbols (IDs 1-999)
// v4.2.2: Added thin-liquid alts for better microstructure opportunities
// ─────────────────────────────────────────────────────────────────────────────
namespace BinanceSymbols {
    constexpr SymbolId INVALID  = 0;
    constexpr SymbolId BTCUSDT  = 1;
    constexpr SymbolId ETHUSDT  = 2;
    constexpr SymbolId SOLUSDT  = 3;
    constexpr SymbolId BNBUSDT  = 4;
    constexpr SymbolId XRPUSDT  = 5;
    constexpr SymbolId ADAUSDT  = 6;
    constexpr SymbolId DOGEUSDT = 7;
    constexpr SymbolId MATICUSDT = 8;
    constexpr SymbolId DOTUSDT  = 9;
    constexpr SymbolId LTCUSDT  = 10;
    // v4.2.2: Thin-liquid alts (worse microstructure = more opportunities)
    constexpr SymbolId AVAXUSDT = 11;
    constexpr SymbolId LINKUSDT = 12;
    constexpr SymbolId OPUSDT   = 13;
    constexpr SymbolId ARBUSDT  = 14;
    
    constexpr SymbolId MIN_ID = 1;
    constexpr SymbolId MAX_ID = 999;
    
    // Check if ID is a Binance symbol
    [[nodiscard]] constexpr bool is_binance(SymbolId id) noexcept {
        return id >= MIN_ID && id <= MAX_ID;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// cTrader/CFD Symbols (IDs 1000-1999)
// ─────────────────────────────────────────────────────────────────────────────
namespace CTraderSymbols {
    constexpr SymbolId XAUUSD   = 1000;  // Gold
    constexpr SymbolId XAGUSD   = 1001;  // Silver
    constexpr SymbolId EURUSD   = 1002;
    constexpr SymbolId GBPUSD   = 1003;
    constexpr SymbolId USDJPY   = 1004;
    constexpr SymbolId AUDUSD   = 1005;
    constexpr SymbolId USDCAD   = 1006;
    constexpr SymbolId USDCHF   = 1007;
    constexpr SymbolId NZDUSD   = 1008;
    constexpr SymbolId EURGBP   = 1009;
    constexpr SymbolId EURJPY   = 1010;
    // Add more as needed...
    
    constexpr SymbolId MIN_ID = 1000;
    constexpr SymbolId MAX_ID = 1999;
    
    // Check if ID is a cTrader symbol
    [[nodiscard]] constexpr bool is_ctrader(SymbolId id) noexcept {
        return id >= MIN_ID && id <= MAX_ID;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Properties (compile-time)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolSpec {
    SymbolId    id;
    const char* name;       // Exchange symbol name (e.g., "BTCUSDT")
    double      tick_size;  // Minimum price increment
    double      lot_size;   // Minimum quantity increment
    double      min_qty;    // Minimum order quantity
    double      max_qty;    // Maximum order quantity
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Table (cold path lookup)
// ─────────────────────────────────────────────────────────────────────────────
namespace SymbolTable {
    
    // Binance specifications
    // v4.2.2: Added thin-liquid alts
    constexpr std::array<SymbolSpec, 14> BINANCE = {{
        { BinanceSymbols::BTCUSDT,   "BTCUSDT",   0.01,    0.00001, 0.00001, 1000.0 },
        { BinanceSymbols::ETHUSDT,   "ETHUSDT",   0.01,    0.0001,  0.0001,  10000.0 },
        { BinanceSymbols::SOLUSDT,   "SOLUSDT",   0.01,    0.01,    0.01,    100000.0 },
        { BinanceSymbols::BNBUSDT,   "BNBUSDT",   0.01,    0.001,   0.001,   10000.0 },
        { BinanceSymbols::XRPUSDT,   "XRPUSDT",   0.0001,  0.1,     0.1,     10000000.0 },
        { BinanceSymbols::ADAUSDT,   "ADAUSDT",   0.0001,  0.1,     0.1,     10000000.0 },
        { BinanceSymbols::DOGEUSDT,  "DOGEUSDT",  0.00001, 1.0,     1.0,     100000000.0 },
        { BinanceSymbols::MATICUSDT, "MATICUSDT", 0.0001,  0.1,     0.1,     10000000.0 },
        { BinanceSymbols::DOTUSDT,   "DOTUSDT",   0.001,   0.01,    0.01,    1000000.0 },
        { BinanceSymbols::LTCUSDT,   "LTCUSDT",   0.01,    0.001,   0.001,   100000.0 },
        // v4.2.2: Thin-liquid alts
        { BinanceSymbols::AVAXUSDT,  "AVAXUSDT",  0.01,    0.01,    0.01,    100000.0 },
        { BinanceSymbols::LINKUSDT,  "LINKUSDT",  0.001,   0.01,    0.01,    100000.0 },
        { BinanceSymbols::OPUSDT,    "OPUSDT",    0.001,   0.1,     0.1,     1000000.0 },
        { BinanceSymbols::ARBUSDT,   "ARBUSDT",   0.0001,  0.1,     0.1,     1000000.0 },
    }};
    
    // cTrader specifications
    constexpr std::array<SymbolSpec, 10> CTRADER = {{
        { CTraderSymbols::XAUUSD, "XAUUSD", 0.01,    0.01, 0.01, 100.0 },
        { CTraderSymbols::XAGUSD, "XAGUSD", 0.001,   0.01, 0.01, 1000.0 },
        { CTraderSymbols::EURUSD, "EURUSD", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::GBPUSD, "GBPUSD", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::USDJPY, "USDJPY", 0.001,   0.01, 0.01, 100.0 },
        { CTraderSymbols::AUDUSD, "AUDUSD", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::USDCAD, "USDCAD", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::USDCHF, "USDCHF", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::NZDUSD, "NZDUSD", 0.00001, 0.01, 0.01, 100.0 },
        { CTraderSymbols::EURGBP, "EURGBP", 0.00001, 0.01, 0.01, 100.0 },
    }};
    
    // Lookup by ID (cold path - linear search is fine for small tables)
    [[nodiscard]] inline const SymbolSpec* find(SymbolId id) noexcept {
        if (BinanceSymbols::is_binance(id)) {
            for (const auto& s : BINANCE) {
                if (s.id == id) return &s;
            }
        } else if (CTraderSymbols::is_ctrader(id)) {
            for (const auto& s : CTRADER) {
                if (s.id == id) return &s;
            }
        }
        return nullptr;
    }
    
    // Lookup by name (cold path - linear search)
    [[nodiscard]] inline const SymbolSpec* find(std::string_view name) noexcept {
        for (const auto& s : BINANCE) {
            if (name == s.name) return &s;
        }
        for (const auto& s : CTRADER) {
            if (name == s.name) return &s;
        }
        return nullptr;
    }
}

} // namespace Chimera
