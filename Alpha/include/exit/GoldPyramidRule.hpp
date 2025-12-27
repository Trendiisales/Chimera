// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Gold Pyramid Rule
// ═══════════════════════════════════════════════════════════════════════════════
// THE ONE PYRAMID TRIGGER (GOLD-ONLY, ONE TIME)
// ANYTHING ELSE IS BANNED.
//
// ❌ BAD PYRAMID TRIGGERS (NEVER USE):
// - Pullbacks
// - Micro consolidations
// - Time-based adds
// - Fixed-distance adds
// - ATR-based adds
//
// All of these:
// - Increase risk before confirmation
// - Amplify noise
// - Raise drawdowns
//
// ✅ THE ONLY GOOD PYRAMID TRIGGER:
// Add ONLY after a confirmed expansion break with volatility expansion.
//
// ┌──────────────────────────────────────────────────────────────┐
// │                 PYRAMID REQUIREMENTS (ALL MUST BE TRUE)      │
// ├──────────────────────────────────────────────────────────────┤
// │ Trade is risk-free (stop at BE)          │ YES              │
// │ open_r ≥                                  │ +1.0R            │
// │ ATR ratio ≥                               │ 1.4              │
// │ Expansion high/low broken                 │ YES              │
// │ Session = London or NY                    │ YES              │
// │ Add count                                 │ 1 max            │
// └──────────────────────────────────────────────────────────────┘
//
// PYRAMID CONSTRAINTS (ABSOLUTE):
// - One add only
// - Add size ≤ 50% of initial
// - No stop tightening
// - No second add
// - Disabled in Asia
//
// This is REINFORCEMENT, not leverage.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../exit/ExitLogic.hpp"
#include "../session/SessionDetector.hpp"

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// PYRAMID DECISION STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════

struct GoldPyramidDecision {
    bool allowed = false;
    double add_size_fraction = 0.0;  // As fraction of initial size (max 0.5)
    const char* reason = "";
};

// ═══════════════════════════════════════════════════════════════════════════════
// THE ONE PYRAMID RULE (GOLD-ONLY)
// ═══════════════════════════════════════════════════════════════════════════════

inline bool gold_pyramid_allowed(
    const PositionState& p,
    double atr_ratio,
    bool expansion_break,
    bool good_session
) {
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Already scaled = no more adds
    // ═══════════════════════════════════════════════════════════════════════════
    if (p.scaled)
        return false;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Must be risk-free (stop at breakeven)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!p.risk_free)
        return false;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Minimum R-multiple required
    // ═══════════════════════════════════════════════════════════════════════════
    if (p.open_r < 1.0)
        return false;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Volatility expansion required
    // ═══════════════════════════════════════════════════════════════════════════
    if (atr_ratio < 1.4)
        return false;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Expansion break required
    // ═══════════════════════════════════════════════════════════════════════════
    if (!expansion_break)
        return false;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // HARD BLOCK: Good session required (London or NY)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!good_session)
        return false;
    
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FULL PYRAMID DECISION WITH SIZING
// ═══════════════════════════════════════════════════════════════════════════════

inline GoldPyramidDecision evaluate_gold_pyramid(
    const PositionState& p,
    double atr_ratio,
    double expansion_high,
    double expansion_low,
    double current_price,
    SessionType session
) {
    GoldPyramidDecision d;
    
    // Check if session is good (London or NY opens)
    bool good_session = (session == SessionType::LONDON_OPEN ||
                         session == SessionType::US_DATA ||
                         session == SessionType::CASH_OPEN);
    
    // Check for expansion break
    bool expansion_break = false;
    if (p.side > 0) {
        // LONG: price must break above expansion high
        expansion_break = (current_price > expansion_high);
    } else if (p.side < 0) {
        // SHORT: price must break below expansion low
        expansion_break = (current_price < expansion_low);
    }
    
    // Already scaled?
    if (p.scaled) {
        d.reason = "ALREADY_SCALED";
        return d;
    }
    
    // Must be risk-free
    if (!p.risk_free) {
        d.reason = "NOT_RISK_FREE";
        return d;
    }
    
    // Minimum R
    if (p.open_r < 1.0) {
        d.reason = "INSUFFICIENT_R";
        return d;
    }
    
    // ATR expansion
    if (atr_ratio < 1.4) {
        d.reason = "NO_VOL_EXPANSION";
        return d;
    }
    
    // Expansion break
    if (!expansion_break) {
        d.reason = "NO_EXPANSION_BREAK";
        return d;
    }
    
    // Good session
    if (!good_session) {
        d.reason = "BAD_SESSION";
        return d;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // ALL CONDITIONS MET - PYRAMID ALLOWED
    // ═══════════════════════════════════════════════════════════════════════════
    d.allowed = true;
    d.add_size_fraction = 0.5;  // Add 50% of initial size
    d.reason = "EXPANSION_CONFIRMED";
    
    return d;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE FUNCTION WITH AUTOMATIC SESSION DETECTION
// ═══════════════════════════════════════════════════════════════════════════════

inline GoldPyramidDecision check_gold_pyramid(
    const PositionState& p,
    double atr_ratio,
    double expansion_high,
    double expansion_low,
    double current_price
) {
    SessionType session = current_session_type();
    return evaluate_gold_pyramid(p, atr_ratio, expansion_high, expansion_low, current_price, session);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PYRAMID SIZING RULES (LOCKED)
// ═══════════════════════════════════════════════════════════════════════════════

constexpr double GOLD_PYRAMID_MAX_SIZE_FRACTION = 0.50;  // ≤50% of initial
constexpr int GOLD_PYRAMID_MAX_ADDS = 1;                 // One add only

inline double calculate_pyramid_size(double initial_size) {
    return initial_size * GOLD_PYRAMID_MAX_SIZE_FRACTION;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HARD GUARANTEES (ENFORCED BY THIS FILE)
// ═══════════════════════════════════════════════════════════════════════════════
// ✅ One add only
// ✅ Add size ≤ 50% of initial
// ✅ No stop tightening (handled elsewhere)
// ✅ No second add (scaled flag)
// ✅ Disabled in Asia (session check)
// ✅ Requires risk-free position (stop at BE)
// ✅ Requires +1.0R minimum
// ✅ Requires volatility expansion (ATR ratio ≥ 1.4)
// ✅ Requires expansion break confirmation
// ═══════════════════════════════════════════════════════════════════════════════

}  // namespace Alpha
