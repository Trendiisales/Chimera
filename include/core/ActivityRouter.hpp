// =============================================================================
// ActivityRouter.hpp - v4.8.0 - AUTHORITATIVE SYMBOL ROUTING
// =============================================================================
// PURPOSE: Single source of truth for which symbols trade in which profiles
//
// LIVE TRADING SYMBOLS:
//   XAUUSD  → SCALP-NY + SCALP-LDN
//   NAS100  → SCALP-NY only (respect existing ownership windows)
//   EURUSD  → SCALP-LDN + NY continuation
//   GBPUSD  → SCALP-LDN + NY continuation
//   USDJPY  → SCALP-LDN only
//
// SHADOW / SENSOR ONLY:
//   US30, SPX500, other FX, other indices
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstring>
#include <chrono>

namespace Chimera {

// =============================================================================
// SYMBOL TRADING MODE
// =============================================================================
enum class SymbolMode : uint8_t {
    LIVE = 0,       // Active trading allowed
    SHADOW = 1,     // Paper trading only (logs, no real orders)
    SENSOR = 2,     // Data collection only (no paper trades)
    BLOCKED = 3     // Completely disabled
};

inline const char* symbolModeToString(SymbolMode m) {
    switch (m) {
        case SymbolMode::LIVE:    return "LIVE";
        case SymbolMode::SHADOW:  return "SHADOW";
        case SymbolMode::SENSOR:  return "SENSOR";
        case SymbolMode::BLOCKED: return "BLOCKED";
        default:                  return "UNKNOWN";
    }
}

// =============================================================================
// SYMBOL ROUTING ENTRY
// =============================================================================
struct SymbolRoute {
    const char* symbol;
    SymbolMode mode;
    bool scalp_ny_allowed;
    bool scalp_ldn_allowed;
    bool core_allowed;
    
    // Session-specific cooldowns (in milliseconds)
    uint32_t shock_cooldown_ms;
};

// =============================================================================
// AUTHORITATIVE ROUTING TABLE
// =============================================================================
namespace ActivityRouter {

// The routing table - SINGLE SOURCE OF TRUTH
inline const SymbolRoute& getRoute(const char* symbol) {
    // XAUUSD - Gold - Primary scalp
    static const SymbolRoute XAUUSD_ROUTE = {
        "XAUUSD",
        SymbolMode::LIVE,
        true,   // SCALP-NY allowed
        true,   // SCALP-LDN allowed
        true,   // CORE allowed
        180000  // 180s shock cooldown
    };
    
    // NAS100 - Index - NY only
    static const SymbolRoute NAS100_ROUTE = {
        "NAS100",
        SymbolMode::LIVE,
        true,   // SCALP-NY allowed
        false,  // SCALP-LDN NOT allowed (Income engine owns London)
        true,   // CORE allowed
        120000  // 120s shock cooldown
    };
    
    // EURUSD - FX - London + NY continuation
    static const SymbolRoute EURUSD_ROUTE = {
        "EURUSD",
        SymbolMode::LIVE,
        true,   // SCALP-NY continuation allowed
        true,   // SCALP-LDN allowed
        false,  // CORE not allowed
        90000   // 90s shock cooldown
    };
    
    // GBPUSD - FX - London + NY continuation
    static const SymbolRoute GBPUSD_ROUTE = {
        "GBPUSD",
        SymbolMode::LIVE,
        true,   // SCALP-NY continuation allowed
        true,   // SCALP-LDN allowed
        false,  // CORE not allowed
        90000   // 90s shock cooldown
    };
    
    // USDJPY - FX - London only
    static const SymbolRoute USDJPY_ROUTE = {
        "USDJPY",
        SymbolMode::LIVE,
        false,  // SCALP-NY NOT allowed
        true,   // SCALP-LDN allowed
        false,  // CORE not allowed
        90000   // 90s shock cooldown
    };
    
    // US30 - Index - SHADOW ONLY
    static const SymbolRoute US30_ROUTE = {
        "US30",
        SymbolMode::SHADOW,
        false,
        false,
        false,
        120000
    };
    
    // SPX500 - Index - SHADOW ONLY
    static const SymbolRoute SPX500_ROUTE = {
        "SPX500",
        SymbolMode::SHADOW,
        false,
        false,
        false,
        120000
    };
    
    // XAGUSD - Silver - SHADOW ONLY
    static const SymbolRoute XAGUSD_ROUTE = {
        "XAGUSD",
        SymbolMode::SHADOW,
        false,
        false,
        false,
        180000
    };
    
    // Default blocked route
    static const SymbolRoute BLOCKED_ROUTE = {
        "BLOCKED",
        SymbolMode::BLOCKED,
        false,
        false,
        false,
        0
    };
    
    // Route lookup
    if (strcmp(symbol, "XAUUSD") == 0) return XAUUSD_ROUTE;
    if (strcmp(symbol, "NAS100") == 0) return NAS100_ROUTE;
    if (strcmp(symbol, "EURUSD") == 0) return EURUSD_ROUTE;
    if (strcmp(symbol, "GBPUSD") == 0) return GBPUSD_ROUTE;
    if (strcmp(symbol, "USDJPY") == 0) return USDJPY_ROUTE;
    if (strcmp(symbol, "US30") == 0)   return US30_ROUTE;
    if (strcmp(symbol, "SPX500") == 0) return SPX500_ROUTE;
    if (strcmp(symbol, "XAGUSD") == 0) return XAGUSD_ROUTE;
    
    return BLOCKED_ROUTE;
}

// =============================================================================
// PROFILE SELECTION (Uses routing table + session)
// =============================================================================

// Session stability tracking for Fix #1
struct SessionStability {
    Session last_session = Session::OFF_HOURS;
    uint64_t stable_since_ns = 0;
    uint32_t transition_count = 0;
    
    // Session must be stable for at least 30 seconds before profile selection
    static constexpr uint64_t STABILITY_THRESHOLD_NS = 30ULL * 1000000000ULL;
    
    bool isStable(Session current, uint64_t now_ns) {
        if (current != last_session) {
            // Session changed - reset stability timer
            last_session = current;
            stable_since_ns = now_ns;
            transition_count++;
            return false;
        }
        
        // Check if stable long enough
        return (now_ns - stable_since_ns) >= STABILITY_THRESHOLD_NS;
    }
    
    uint64_t timeUntilStable(uint64_t now_ns) const {
        if (now_ns >= stable_since_ns + STABILITY_THRESHOLD_NS) return 0;
        return (stable_since_ns + STABILITY_THRESHOLD_NS) - now_ns;
    }
};

// Global session stability tracker
inline SessionStability& getSessionStability() {
    static SessionStability stability;
    return stability;
}

// Check if session is resolved (stable) - REQUIRED CHECK BEFORE PROFILE SELECTION
inline bool isSessionResolved(Session session, uint64_t now_ns = 0) {
    if (now_ns == 0) {
        now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    return getSessionStability().isStable(session, now_ns);
}

inline ActivityProfile selectProfile(const char* symbol, Session session, uint64_t now_ns = 0) {
    const SymbolRoute& route = getRoute(symbol);
    
    // Mode check first
    if (route.mode != SymbolMode::LIVE) {
        return ActivityProfile::DISABLED;
    }
    
    // =========================================================================
    // FIX #1: ENSURE SESSION RESOLVES BEFORE PROFILE SELECTION
    // =========================================================================
    // If session is not yet stable, return DISABLED to prevent wrong profile
    // This prevents LDN/NY confusion during session transitions
    if (now_ns == 0) {
        now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    
    if (!isSessionResolved(session, now_ns)) {
        // Session not yet stable - do not select profile yet
        return ActivityProfile::DISABLED;
    }
    // =========================================================================
    
    // Daily limits check
    if (!ScalpDailyTracker::instance().isScalpAllowed()) {
        // Scalp disabled, fall back to CORE if allowed
        return route.core_allowed ? ActivityProfile::CORE : ActivityProfile::DISABLED;
    }
    
    // Session-based profile selection
    switch (session) {
        case Session::NY_OPEN:
        case Session::NY_CONTINUATION:
            if (route.scalp_ny_allowed) {
                return ActivityProfile::SCALP_NY;
            }
            break;
            
        case Session::LONDON:
            if (route.scalp_ldn_allowed) {
                return ActivityProfile::SCALP_LDN;
            }
            break;
            
        case Session::ASIA:
        case Session::OFF_HOURS:
        default:
            // No SCALP in Asia/Off-hours
            break;
    }
    
    // Fall back to CORE if allowed, otherwise disabled
    return route.core_allowed ? ActivityProfile::CORE : ActivityProfile::DISABLED;
}

// =============================================================================
// HELPER QUERIES
// =============================================================================
inline bool isLiveSymbol(const char* symbol) {
    return getRoute(symbol).mode == SymbolMode::LIVE;
}

inline bool isShadowSymbol(const char* symbol) {
    return getRoute(symbol).mode == SymbolMode::SHADOW;
}

inline bool isScalpSymbol(const char* symbol) {
    const SymbolRoute& route = getRoute(symbol);
    return route.scalp_ny_allowed || route.scalp_ldn_allowed;
}

inline uint32_t getShockCooldown(const char* symbol) {
    return getRoute(symbol).shock_cooldown_ms;
}

// =============================================================================
// SESSION WINDOW CHECK
// =============================================================================
inline bool isSymbolAllowedInSession(const char* symbol, Session session) {
    const SymbolRoute& route = getRoute(symbol);
    
    if (route.mode != SymbolMode::LIVE) return false;
    
    switch (session) {
        case Session::NY_OPEN:
        case Session::NY_CONTINUATION:
            return route.scalp_ny_allowed || route.core_allowed;
            
        case Session::LONDON:
            return route.scalp_ldn_allowed || route.core_allowed;
            
        case Session::ASIA:
        case Session::OFF_HOURS:
            return route.core_allowed;  // Only CORE in off-hours
            
        default:
            return false;
    }
}

// =============================================================================
// PRINT ROUTING TABLE
// =============================================================================
inline void printRoutingTable() {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  ACTIVITY ROUTING TABLE (v4.8.0)\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Symbol   Mode     SCALP-NY  SCALP-LDN  CORE   Cooldown\n");
    printf("  ─────────────────────────────────────────────────────────────\n");
    
    const char* symbols[] = {"XAUUSD", "NAS100", "EURUSD", "GBPUSD", "USDJPY", "US30", "SPX500"};
    for (const char* sym : symbols) {
        const SymbolRoute& r = getRoute(sym);
        printf("  %-8s %-8s %-9s %-10s %-6s %dms\n",
               r.symbol,
               symbolModeToString(r.mode),
               r.scalp_ny_allowed ? "YES" : "no",
               r.scalp_ldn_allowed ? "YES" : "no",
               r.core_allowed ? "YES" : "no",
               r.shock_cooldown_ms);
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");
}

} // namespace ActivityRouter

} // namespace Chimera
