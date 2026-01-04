// ═══════════════════════════════════════════════════════════════════════════════
// cfd_engine/include/config/BlackBullSpreadTables.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// VERSION: v1.0.0
// OWNER: Jo
//
// DESIGN:
// - Session-aware spread gates for BlackBull Markets
// - Spreads are ABSOLUTE - never override to "see trades"
// - Outside preferred windows → BLOCK (not relax)
// - Each symbol has session-specific max spreads
//
// HARD RULE: Spread gates are absolute. Never override to "see trades."
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <ctime>

namespace chimera {
namespace cfd {
namespace config {

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════════════

enum class TradingSession : uint8_t {
    OFF = 0,            // No trading allowed
    ASIA,               // 00:00-08:00 UTC
    PRE_LONDON,         // 06:00-08:00 UTC
    LONDON_OPEN,        // 08:00-08:30 UTC (first 30 min)
    LONDON,             // 08:00-12:00 UTC
    LONDON_NY_OVERLAP,  // 13:00-16:00 UTC
    NY_OPEN,            // 13:30-14:15 UTC (first 45 min)
    NY_MID,             // 14:15-17:00 UTC
    NY_CLOSE,           // 17:00-21:00 UTC
    POST_NY             // 21:00-00:00 UTC
};

inline const char* session_str(TradingSession s) noexcept {
    switch (s) {
        case TradingSession::OFF:              return "OFF";
        case TradingSession::ASIA:             return "ASIA";
        case TradingSession::PRE_LONDON:       return "PRE_LONDON";
        case TradingSession::LONDON_OPEN:      return "LONDON_OPEN";
        case TradingSession::LONDON:           return "LONDON";
        case TradingSession::LONDON_NY_OVERLAP:return "LONDON_NY";
        case TradingSession::NY_OPEN:          return "NY_OPEN";
        case TradingSession::NY_MID:           return "NY_MID";
        case TradingSession::NY_CLOSE:         return "NY_CLOSE";
        case TradingSession::POST_NY:          return "POST_NY";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// TRADE PERMISSION
// ═══════════════════════════════════════════════════════════════════════════════

enum class TradePermission : uint8_t {
    BLOCKED = 0,        // ❌ No trading
    REDUCED,            // ⚠️ Reduced size (0.5x-0.7x)
    ALLOWED             // ✅ Full size
};

inline const char* permission_str(TradePermission p) noexcept {
    switch (p) {
        case TradePermission::BLOCKED: return "BLOCKED";
        case TradePermission::REDUCED: return "REDUCED";
        case TradePermission::ALLOWED: return "ALLOWED";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION SPREAD ENTRY
// ═══════════════════════════════════════════════════════════════════════════════

struct SessionSpreadEntry {
    TradingSession session;
    double max_spread;          // In native units (pts for indices, pips for FX)
    TradePermission permission;
    double size_multiplier;     // 1.0 = full, 0.5 = half, etc.
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYMBOL SPREAD TABLES (EXACT - FROM BLACKBULL TUNING)
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// NAS100
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 6> NAS100_SPREADS = {{
    { TradingSession::LONDON_OPEN,       1.0, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON_NY_OVERLAP, 1.1, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_OPEN,           1.1, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_MID,            1.3, TradePermission::REDUCED, 0.7 },
    { TradingSession::ASIA,              0.0, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,               0.0, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// SPX500
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 5> SPX500_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.9, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_OPEN,           1.0, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_MID,            1.2, TradePermission::REDUCED, 0.7 },
    { TradingSession::LONDON,            0.0, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,               0.0, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// US30 (Dow Jones)
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 5> US30_SPREADS = {{
    { TradingSession::LONDON_OPEN,       2.3, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_OPEN,           2.4, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_MID,            2.8, TradePermission::BLOCKED, 0.0 }, // Too wide
    { TradingSession::LONDON_NY_OVERLAP, 2.5, TradePermission::REDUCED, 0.7 },
    { TradingSession::OFF,               0.0, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// GER40 (DAX)
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 4> GER40_SPREADS = {{
    { TradingSession::LONDON_OPEN,  1.8, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON,       2.0, TradePermission::REDUCED, 0.7 }, // First 60m EU
    { TradingSession::NY_OPEN,      0.0, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,          0.0, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// UK100 (FTSE)
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 3> UK100_SPREADS = {{
    { TradingSession::LONDON,  1.7, TradePermission::REDUCED, 0.5 }, // MR only
    { TradingSession::NY_OPEN, 0.0, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,     0.0, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// XAUUSD (Gold)
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 5> XAUUSD_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.28, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_OPEN,           0.30, TradePermission::ALLOWED, 1.0 },
    { TradingSession::ASIA,              0.32, TradePermission::REDUCED, 0.6 },
    { TradingSession::LONDON,            0.30, TradePermission::ALLOWED, 1.0 },
    { TradingSession::OFF,               0.00, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// EURUSD
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 4> EURUSD_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.18, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON,            0.23, TradePermission::REDUCED, 0.7 }, // +0.05
    { TradingSession::ASIA,              0.00, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,               0.00, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// GBPUSD
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 4> GBPUSD_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.32, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON,            0.37, TradePermission::REDUCED, 0.7 }, // +0.05
    { TradingSession::ASIA,              0.00, TradePermission::BLOCKED, 0.0 },
    { TradingSession::OFF,               0.00, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// USDJPY
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 5> USDJPY_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.24, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON,            0.29, TradePermission::REDUCED, 0.7 }, // +0.05
    { TradingSession::ASIA,              0.26, TradePermission::REDUCED, 0.6 }, // JPY home
    { TradingSession::NY_OPEN,           0.25, TradePermission::ALLOWED, 1.0 },
    { TradingSession::OFF,               0.00, TradePermission::BLOCKED, 0.0 }
}};

// ─────────────────────────────────────────────────────────────────────────────
// XAGUSD (Silver)
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::array<SessionSpreadEntry, 4> XAGUSD_SPREADS = {{
    { TradingSession::LONDON_NY_OVERLAP, 0.025, TradePermission::ALLOWED, 1.0 },
    { TradingSession::NY_OPEN,           0.028, TradePermission::ALLOWED, 1.0 },
    { TradingSession::LONDON,            0.030, TradePermission::REDUCED, 0.6 },
    { TradingSession::OFF,               0.000, TradePermission::BLOCKED, 0.0 }
}};

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION DETECTION
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Determine current trading session from UTC hour and minute
 */
inline TradingSession get_current_session(int utc_hour, int utc_minute = 0) noexcept {
    // Convert to minutes since midnight for precise session boundaries
    int mins = utc_hour * 60 + utc_minute;
    
    // Session boundaries (in minutes from midnight UTC)
    // Asia: 00:00-06:00
    if (mins >= 0 && mins < 360) {
        return TradingSession::ASIA;
    }
    // Pre-London: 06:00-08:00
    if (mins >= 360 && mins < 480) {
        return TradingSession::PRE_LONDON;
    }
    // London Open: 08:00-08:30
    if (mins >= 480 && mins < 510) {
        return TradingSession::LONDON_OPEN;
    }
    // London: 08:30-13:00
    if (mins >= 510 && mins < 780) {
        return TradingSession::LONDON;
    }
    // NY Open: 13:30-14:15
    if (mins >= 810 && mins < 855) {
        return TradingSession::NY_OPEN;
    }
    // London-NY Overlap: 13:00-16:00 (excluding NY Open window)
    if (mins >= 780 && mins < 960) {
        if (mins >= 810 && mins < 855) {
            return TradingSession::NY_OPEN;
        }
        return TradingSession::LONDON_NY_OVERLAP;
    }
    // NY Mid: 16:00-17:00
    if (mins >= 960 && mins < 1020) {
        return TradingSession::NY_MID;
    }
    // NY Close: 17:00-21:00
    if (mins >= 1020 && mins < 1260) {
        return TradingSession::NY_CLOSE;
    }
    // Post-NY: 21:00-00:00
    if (mins >= 1260 && mins < 1440) {
        return TradingSession::POST_NY;
    }
    
    return TradingSession::OFF;
}

/**
 * Get current session from system time
 */
inline TradingSession get_current_session_now() noexcept {
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    if (!utc) return TradingSession::OFF;
    return get_current_session(utc->tm_hour, utc->tm_min);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SPREAD GATE CHECKER
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Result of spread gate check
 */
struct SpreadGateResult {
    bool allowed;                   // Can we trade?
    double max_spread;              // Maximum allowed spread
    double size_multiplier;         // Position size multiplier
    TradingSession session;         // Current session
    TradePermission permission;     // Permission level
    const char* block_reason;       // If blocked, why
    
    explicit operator bool() const noexcept { return allowed; }
};

/**
 * Check spread gate for a symbol
 * 
 * @param symbol Symbol name (e.g., "NAS100", "XAUUSD")
 * @param current_spread Current market spread
 * @param utc_hour Current UTC hour
 * @param utc_minute Current UTC minute
 * @return SpreadGateResult with decision and metadata
 */
inline SpreadGateResult check_spread_gate(
    const char* symbol,
    double current_spread,
    int utc_hour,
    int utc_minute = 0
) noexcept {
    SpreadGateResult result;
    result.session = get_current_session(utc_hour, utc_minute);
    result.allowed = false;
    result.max_spread = 0.0;
    result.size_multiplier = 0.0;
    result.permission = TradePermission::BLOCKED;
    result.block_reason = "SESSION_BLOCKED";
    
    // Helper lambda to check spread table
    auto check_table = [&](const auto& table) {
        for (const auto& entry : table) {
            if (entry.session == result.session || 
                (entry.session == TradingSession::OFF && result.permission == TradePermission::BLOCKED)) {
                
                if (entry.permission == TradePermission::BLOCKED) {
                    result.allowed = false;
                    result.block_reason = "SESSION_BLOCKED";
                    return;
                }
                
                result.max_spread = entry.max_spread;
                result.size_multiplier = entry.size_multiplier;
                result.permission = entry.permission;
                
                if (current_spread <= entry.max_spread) {
                    result.allowed = true;
                    result.block_reason = nullptr;
                } else {
                    result.allowed = false;
                    result.block_reason = "SPREAD_TOO_WIDE";
                }
                return;
            }
        }
    };
    
    // Match symbol to spread table
    if (strcmp(symbol, "NAS100") == 0) {
        check_table(NAS100_SPREADS);
    } else if (strcmp(symbol, "SPX500") == 0) {
        check_table(SPX500_SPREADS);
    } else if (strcmp(symbol, "US30") == 0) {
        check_table(US30_SPREADS);
    } else if (strcmp(symbol, "GER40") == 0) {
        check_table(GER40_SPREADS);
    } else if (strcmp(symbol, "UK100") == 0) {
        check_table(UK100_SPREADS);
    } else if (strcmp(symbol, "XAUUSD") == 0) {
        check_table(XAUUSD_SPREADS);
    } else if (strcmp(symbol, "EURUSD") == 0) {
        check_table(EURUSD_SPREADS);
    } else if (strcmp(symbol, "GBPUSD") == 0) {
        check_table(GBPUSD_SPREADS);
    } else if (strcmp(symbol, "USDJPY") == 0) {
        check_table(USDJPY_SPREADS);
    } else if (strcmp(symbol, "XAGUSD") == 0) {
        check_table(XAGUSD_SPREADS);
    } else {
        result.block_reason = "UNKNOWN_SYMBOL";
    }
    
    return result;
}

/**
 * Convenience overload using current system time
 */
inline SpreadGateResult check_spread_gate_now(
    const char* symbol,
    double current_spread
) noexcept {
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    if (!utc) {
        SpreadGateResult result;
        result.allowed = false;
        result.block_reason = "TIME_ERROR";
        return result;
    }
    return check_spread_gate(symbol, current_spread, utc->tm_hour, utc->tm_min);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION MULTIPLIERS (FOR POSITION SIZING)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Get session-based position size multiplier
 * These apply to SIZE, not to spread thresholds
 */
inline double get_session_size_multiplier(TradingSession session) noexcept {
    switch (session) {
        case TradingSession::ASIA:             return 0.6;
        case TradingSession::PRE_LONDON:       return 0.8;
        case TradingSession::LONDON_OPEN:      return 1.4;
        case TradingSession::LONDON:           return 1.0;
        case TradingSession::LONDON_NY_OVERLAP:return 1.2;
        case TradingSession::NY_OPEN:          return 1.6;
        case TradingSession::NY_MID:           return 1.0;
        case TradingSession::NY_CLOSE:         return 0.8;
        case TradingSession::POST_NY:          return 0.7;
        case TradingSession::OFF:              return 0.0;
        default: return 0.0;
    }
}

/**
 * Get combined size multiplier (session × spread gate permission)
 */
inline double get_combined_size_multiplier(
    TradingSession session,
    TradePermission permission
) noexcept {
    double session_mult = get_session_size_multiplier(session);
    double permission_mult = 0.0;
    
    switch (permission) {
        case TradePermission::ALLOWED: permission_mult = 1.0; break;
        case TradePermission::REDUCED: permission_mult = 0.7; break;
        case TradePermission::BLOCKED: permission_mult = 0.0; break;
    }
    
    return session_mult * permission_mult;
}

} // namespace config
} // namespace cfd
} // namespace chimera
