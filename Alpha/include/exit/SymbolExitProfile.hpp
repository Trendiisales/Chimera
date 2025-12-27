// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Symbol Exit Profile
// ═══════════════════════════════════════════════════════════════════════════════
// REALITY:
// - XAUUSD trends cleaner, retraces deeper
// - NAS100 expands fast, mean-reverts harder
// - Exits MUST be symbol-specific, not global
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include "session/SessionDetector.hpp"

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// EXIT PROFILE STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════

struct ExitProfile {
    double winner_protect_r;    // R-multiple above which we NEVER exit
    double exit_edge_tol;       // Edge tolerance (more negative = more patient)
    uint64_t min_hold_ms;       // Minimum hold time in milliseconds
    double breakeven_r;         // R-multiple to move stop to entry
    double scale_r;             // R-multiple to allow scaling
};

// ═══════════════════════════════════════════════════════════════════════════════
// XAUUSD EXIT PROFILE
// ═══════════════════════════════════════════════════════════════════════════════
// Gold trends cleaner, can hold through deeper retracements
// NY session is best - maximum patience

inline ExitProfile xauusd_exit_profile(SessionType session) {
    bool is_prime = (session == SessionType::LONDON_OPEN || 
                     session == SessionType::US_DATA);
    
    return {
        .winner_protect_r = 0.7,                              // Protect at +0.7R
        .exit_edge_tol = is_prime ? -1.4 : -1.1,              // More patient in prime
        .min_hold_ms = is_prime ? 2400ULL : 2000ULL,          // Longer holds in prime
        .breakeven_r = 0.3,                                   // Move stop at +0.3R
        .scale_r = 0.5                                        // Scale at +0.5R
    };
}

// ═══════════════════════════════════════════════════════════════════════════════
// NAS100 EXIT PROFILE
// ═══════════════════════════════════════════════════════════════════════════════
// Index expands fast, mean-reverts harder
// Cash open/power hour are best - but still tighter than gold

inline ExitProfile nas100_exit_profile(SessionType session) {
    bool is_prime = (session == SessionType::CASH_OPEN || 
                     session == SessionType::POWER_HOUR);
    
    return {
        .winner_protect_r = 0.6,                              // Protect at +0.6R (tighter)
        .exit_edge_tol = is_prime ? -1.2 : -0.9,              // Less patient than gold
        .min_hold_ms = is_prime ? 1800ULL : 1500ULL,          // Shorter holds
        .breakeven_r = 0.25,                                  // Move stop earlier
        .scale_r = 0.4                                        // Scale earlier
    };
}

// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT EXIT PROFILE (unknown symbols)
// ═══════════════════════════════════════════════════════════════════════════════

inline ExitProfile default_exit_profile() {
    return {
        .winner_protect_r = 0.5,
        .exit_edge_tol = -0.6,
        .min_hold_ms = 1200,
        .breakeven_r = 0.3,
        .scale_r = 0.5
    };
}

// ═══════════════════════════════════════════════════════════════════════════════
// EXIT PROFILE SELECTOR
// ═══════════════════════════════════════════════════════════════════════════════

inline ExitProfile exit_profile(const std::string& symbol, SessionType session) {
    if (symbol == "XAUUSD")
        return xauusd_exit_profile(session);
    
    if (symbol == "NAS100" || symbol == "US100" || symbol == "USTEC")
        return nas100_exit_profile(session);
    
    return default_exit_profile();
}

}  // namespace Alpha
