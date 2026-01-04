#pragma once
// =============================================================================
// VenueRoutingPolicy.hpp v4.2.2 - Venue-Aware Execution Routing
// =============================================================================
// Decides where and how to execute per symbol, per edge quality:
//   - FIX vs venue-native routing
//   - Order type selection (IOC/FOK/Post-Only/Limit)
//   - Latency-aware blocking
// =============================================================================

#include <string>
#include <cstring>
#include <cstdint>

namespace Omega {

// =============================================================================
// EXECUTION VENUE
// =============================================================================
enum class ExecutionVenue : uint8_t {
    FIX = 0,
    VENUE_NATIVE = 1,
    BLOCKED = 2
};

inline const char* ExecutionVenueStr(ExecutionVenue v) {
    switch (v) {
        case ExecutionVenue::FIX: return "FIX";
        case ExecutionVenue::VENUE_NATIVE: return "VENUE_NATIVE";
        case ExecutionVenue::BLOCKED: return "BLOCKED";
    }
    return "UNKNOWN";
}

// =============================================================================
// ORDER TYPE
// =============================================================================
enum class OrderType : uint8_t {
    MARKET = 0,
    LIMIT = 1,
    LIMIT_POST_ONLY = 2,
    IOC = 3,
    FOK = 4,
    BLOCKED = 5
};

inline const char* OrderTypeStr(OrderType t) {
    switch (t) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::LIMIT_POST_ONLY: return "POST_ONLY";
        case OrderType::IOC: return "IOC";
        case OrderType::FOK: return "FOK";
        case OrderType::BLOCKED: return "BLOCKED";
    }
    return "UNKNOWN";
}

// =============================================================================
// EXECUTION STYLE
// =============================================================================
enum class ExecutionStyle : uint8_t {
    PASSIVE = 0,
    AGGRESSIVE = 1,
    BLOCKED = 2
};

// =============================================================================
// VENUE ROUTING RULE (with fallback and disable conditions)
// =============================================================================
struct VenueRoutingRule {
    double min_edge;           // Minimum edge required
    double max_latency_ms;     // Hard latency cutoff
    bool allow_aggressive;     // Can cross / market
    bool allow_passive;        // Can post / join
};

// =============================================================================
// VENUE ROUTING ENTRY - Complete routing table entry
// Per Document 7: Symbol | Preferred | Fallback | Disabled When
// =============================================================================
struct VenueRoutingEntry {
    const char* symbol;
    ExecutionVenue preferred;
    ExecutionVenue fallback;
    
    // Disable conditions
    double max_spread_bps;     // Disabled when spread > this
    double max_latency_ms;     // Disabled when latency > this
    bool disable_on_news;      // Disabled on news flag
    bool disable_on_desync;    // Disabled on book desync
    
    VenueRoutingRule fix_rule;
    VenueRoutingRule venue_rule;
};

// =============================================================================
// ROUTING TABLE (Authoritative per Document 7)
// =============================================================================
// Symbol   | Preferred   | Fallback   | Disabled When
// BTCUSDT  | Binance WS  | REST       | WS lag
// ETHUSDT  | Binance WS  | REST       | book desync
// XAUUSD   | FIX         | NONE       | spread > threshold
// NAS100   | FIX         | NONE       | news flag
// EURUSD   | FIX         | NONE       | latency spike
// =============================================================================

inline constexpr VenueRoutingEntry BTCUSDT_ROUTING = {
    "BTCUSDT",
    ExecutionVenue::VENUE_NATIVE,  // Preferred: Binance WS
    ExecutionVenue::BLOCKED,       // Fallback: REST (represented as BLOCKED for safety)
    2.0,    // max_spread_bps
    50.0,   // max_latency_ms
    false,  // disable_on_news
    true,   // disable_on_desync
    { 999.0, 0.0, false, false },  // FIX: Never use
    { 0.3, 100.0, true, true }     // VENUE: min 0.3 edge, 100ms max
};

inline constexpr VenueRoutingEntry ETHUSDT_ROUTING = {
    "ETHUSDT",
    ExecutionVenue::VENUE_NATIVE,
    ExecutionVenue::BLOCKED,
    3.0, 50.0, false, true,
    { 999.0, 0.0, false, false },
    { 0.4, 100.0, true, true }
};

inline constexpr VenueRoutingEntry XAUUSD_ROUTING = {
    "XAUUSD",
    ExecutionVenue::FIX,
    ExecutionVenue::BLOCKED,  // NO fallback for CFDs (catastrophic slippage)
    8.0,    // Disabled when spread > 8bps
    8.0,    // Disabled when latency > 8ms
    true,   // Disabled on news
    true,   // Disabled on desync
    { 1.5, 8.0, true, false },   // FIX: min 1.5 edge, 8ms max, aggressive only
    { 999.0, 0.0, false, false } // VENUE: Never use
};

inline constexpr VenueRoutingEntry NAS100_ROUTING_ENTRY = {
    "NAS100",
    ExecutionVenue::FIX,
    ExecutionVenue::BLOCKED,  // NO fallback for CFDs
    3.0,    // Disabled when spread > 3bps
    12.0,   // Disabled when latency > 12ms
    true,   // Disabled on news
    true,   // Disabled on desync
    { 0.8, 12.0, true, true },
    { 999.0, 0.0, false, false }
};

inline constexpr VenueRoutingEntry EURUSD_ROUTING = {
    "EURUSD",
    ExecutionVenue::FIX,
    ExecutionVenue::BLOCKED,  // NO fallback
    2.0,
    6.0,    // Disabled on latency spike (very sensitive)
    true,
    true,
    { 2.0, 6.0, true, false },
    { 999.0, 0.0, false, false }
};

// Default routing for unknown symbols (conservative)
inline constexpr VenueRoutingEntry DEFAULT_ROUTING = {
    "DEFAULT",
    ExecutionVenue::BLOCKED,
    ExecutionVenue::BLOCKED,
    1.0, 5.0, true, true,
    { 999.0, 0.0, false, false },
    { 999.0, 0.0, false, false }
};

// =============================================================================
// GET ROUTING ENTRY
// =============================================================================
inline const VenueRoutingEntry& GetRoutingEntry(const char* symbol) {
    if (strstr(symbol, "BTCUSDT")) return BTCUSDT_ROUTING;
    if (strstr(symbol, "ETHUSDT")) return ETHUSDT_ROUTING;
    if (strstr(symbol, "XAUUSD") || strstr(symbol, "XAGUSD")) return XAUUSD_ROUTING;
    if (strstr(symbol, "NAS100") || strstr(symbol, "US100")) return NAS100_ROUTING_ENTRY;
    if (strstr(symbol, "EURUSD") || strstr(symbol, "GBPUSD") ||
        strstr(symbol, "USDJPY") || strstr(symbol, "AUDUSD")) return EURUSD_ROUTING;
    return DEFAULT_ROUTING;
}

// =============================================================================
// CHECK IF VENUE IS DISABLED
// =============================================================================
inline bool IsVenueDisabled(
    const char* symbol,
    double spread_bps,
    double latency_ms,
    bool news_active,
    bool book_desynced
) {
    const auto& entry = GetRoutingEntry(symbol);
    
    if (spread_bps > entry.max_spread_bps) return true;
    if (latency_ms > entry.max_latency_ms) return true;
    if (entry.disable_on_news && news_active) return true;
    if (entry.disable_on_desync && book_desynced) return true;
    
    return false;
}

// =============================================================================
// CHOOSE EXECUTION VENUE (uses new routing entries)
// =============================================================================
inline ExecutionVenue ChooseExecutionVenue(
    const char* symbol,
    double net_edge,
    double latency_ms,
    bool aggressive,
    double spread_bps = 0.0,
    bool news_active = false,
    bool book_desynced = false
) {
    const auto& entry = GetRoutingEntry(symbol);
    
    // Check disable conditions first
    if (IsVenueDisabled(symbol, spread_bps, latency_ms, news_active, book_desynced)) {
        return ExecutionVenue::BLOCKED;
    }
    
    // Try preferred venue first
    if (entry.preferred == ExecutionVenue::VENUE_NATIVE) {
        if (net_edge >= entry.venue_rule.min_edge &&
            latency_ms <= entry.venue_rule.max_latency_ms) {
            if ((aggressive && entry.venue_rule.allow_aggressive) ||
                (!aggressive && entry.venue_rule.allow_passive)) {
                return ExecutionVenue::VENUE_NATIVE;
            }
        }
    } else if (entry.preferred == ExecutionVenue::FIX) {
        if (net_edge >= entry.fix_rule.min_edge &&
            latency_ms <= entry.fix_rule.max_latency_ms) {
            if ((aggressive && entry.fix_rule.allow_aggressive) ||
                (!aggressive && entry.fix_rule.allow_passive)) {
                return ExecutionVenue::FIX;
            }
        }
    }
    
    // Try fallback (Note: CFDs should have BLOCKED fallback per Document 7)
    if (entry.fallback != ExecutionVenue::BLOCKED) {
        if (entry.fallback == ExecutionVenue::VENUE_NATIVE) {
            if (net_edge >= entry.venue_rule.min_edge &&
                latency_ms <= entry.venue_rule.max_latency_ms) {
                return ExecutionVenue::VENUE_NATIVE;
            }
        } else if (entry.fallback == ExecutionVenue::FIX) {
            if (net_edge >= entry.fix_rule.min_edge &&
                latency_ms <= entry.fix_rule.max_latency_ms) {
                return ExecutionVenue::FIX;
            }
        }
    }
    
    return ExecutionVenue::BLOCKED;
}

// =============================================================================
// ORDER TYPE THRESHOLDS
// =============================================================================
namespace OrderTypeThresholds {
    constexpr double EDGE_STRONG = 1.5;
    constexpr double EDGE_MEDIUM = 0.8;
    constexpr double LATENCY_FAST_MS = 8.0;
    constexpr double LATENCY_SLOW_MS = 20.0;
}

// =============================================================================
// CHOOSE ORDER TYPE
// =============================================================================
inline OrderType ChooseOrderType(
    const char* symbol,
    bool aggressive,
    double net_edge,
    double latency_ms
) {
    using namespace OrderTypeThresholds;
    
    // Crypto & venue-native symbols tolerate aggressive IOC/FOK
    bool crypto = (strstr(symbol, "USDT") != nullptr);
    
    if (latency_ms > LATENCY_SLOW_MS) {
        return OrderType::BLOCKED;
    }
    
    if (aggressive) {
        if (net_edge >= EDGE_STRONG && latency_ms <= LATENCY_FAST_MS) {
            return crypto ? OrderType::FOK : OrderType::IOC;
        }
        if (net_edge >= EDGE_MEDIUM) {
            return OrderType::IOC;
        }
        return OrderType::BLOCKED;
    }
    
    // Passive execution
    if (net_edge >= EDGE_MEDIUM) {
        return OrderType::LIMIT_POST_ONLY;
    }
    
    return OrderType::LIMIT;
}

// =============================================================================
// CHOOSE EXECUTION STYLE (based on microstructure profile)
// =============================================================================
inline ExecutionStyle ChooseExecutionStyle(
    double net_edge,
    double latency_ms,
    double latency_sensitivity
) {
    double latency_penalty = latency_sensitivity * latency_ms * 0.1;
    double adjusted_edge = net_edge - latency_penalty;
    
    if (adjusted_edge <= 0.0) {
        return ExecutionStyle::BLOCKED;
    }
    
    if (adjusted_edge > 1.5 * latency_penalty && adjusted_edge > 1.0) {
        return ExecutionStyle::AGGRESSIVE;
    }
    
    return ExecutionStyle::PASSIVE;
}

} // namespace Omega
