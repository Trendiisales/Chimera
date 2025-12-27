// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Exit Looseness
// ═══════════════════════════════════════════════════════════════════════════════
// DESIGN RULE:
// - London/NY opens = more patience (winners live longer)
// - Off-session = stricter exits
// - Losers are still cut fast everywhere
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "session/SessionDetector.hpp"

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// EDGE TOLERANCE BY SESSION
// ═══════════════════════════════════════════════════════════════════════════════
// More negative = more tolerant (willing to hold through deeper pullbacks)
// Less negative = stricter (exits faster on edge degradation)

inline double exit_edge_tolerance(SessionType session) {
    switch (session) {
        case SessionType::LONDON_OPEN:  return -1.2;  // Most patient during London
        case SessionType::US_DATA:      return -1.3;  // Very patient during US data
        case SessionType::CASH_OPEN:    return -1.3;  // Patient during cash open
        case SessionType::POWER_HOUR:   return -1.1;  // Patient during power hour
        case SessionType::ASIA:         return -0.4;  // Strict during Asia
        case SessionType::LONDON_PM:    return -0.8;  // Moderate
        case SessionType::NY_AFTERNOON: return -0.6;  // Moderate-strict
        case SessionType::PRE_MARKET:   return -0.6;  // Moderate-strict
        case SessionType::MIDDAY:       return -0.4;  // Strict (midday chop)
        default:                        return -0.4;  // Default strict
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// MINIMUM HOLD TIME BY SESSION (milliseconds)
// ═══════════════════════════════════════════════════════════════════════════════
// Longer = more patient (allows trade to develop)
// Shorter = quicker cuts

inline uint64_t min_hold_ms(SessionType session) {
    switch (session) {
        case SessionType::LONDON_OPEN:  return 1800;  // 1.8s min hold
        case SessionType::US_DATA:      return 2000;  // 2.0s min hold (news volatility)
        case SessionType::CASH_OPEN:    return 2000;  // 2.0s min hold
        case SessionType::POWER_HOUR:   return 1800;  // 1.8s min hold
        case SessionType::ASIA:         return 800;   // Quick cuts in Asia
        case SessionType::LONDON_PM:    return 1200;  // 1.2s
        case SessionType::NY_AFTERNOON: return 1000;  // 1.0s
        case SessionType::PRE_MARKET:   return 1000;  // 1.0s
        case SessionType::MIDDAY:       return 800;   // Quick cuts in midday chop
        default:                        return 800;   // Default quick
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION QUALITY TIER
// ═══════════════════════════════════════════════════════════════════════════════

enum class SessionQuality {
    PRIME,      // Best sessions - maximum patience
    GOOD,       // Good sessions - moderate patience
    MARGINAL,   // Marginal sessions - stricter
    AVOID       // Should not trade - fastest exits if caught
};

inline SessionQuality session_quality(SessionType session) {
    switch (session) {
        case SessionType::LONDON_OPEN:
        case SessionType::US_DATA:
        case SessionType::CASH_OPEN:
        case SessionType::POWER_HOUR:
            return SessionQuality::PRIME;
            
        case SessionType::LONDON_PM:
        case SessionType::NY_AFTERNOON:
            return SessionQuality::GOOD;
            
        case SessionType::PRE_MARKET:
        case SessionType::MIDDAY:
            return SessionQuality::MARGINAL;
            
        case SessionType::ASIA:
        default:
            return SessionQuality::AVOID;
    }
}

}  // namespace Alpha
