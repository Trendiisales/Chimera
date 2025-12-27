// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Exit Logic
// ═══════════════════════════════════════════════════════════════════════════════
// 🔥 THIS IS THE KEY FILE - WINNER-BIASED EXITS
// 
// Core identity change:
// ❌ "Fast in, fast out"
// ✅ "Fast in, patient out"
//
// We explicitly:
// - Enter on impulse
// - Exit on structure
// - Protect winners
// - Kill scratches
// - Let only the right trades expand
//
// ❌ Old behavior:
// - Exits on impulse invalidation
// - Exits on micro noise
// - Scratches endlessly
// - Never pays spread back
//
// ✅ New behavior:
// - Ignores early noise
// - Kills only failed impulses
// - Protects winners
// - Lets structure resolve
// - Turns selectivity into size
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "SymbolExitProfile.hpp"
#include "ExitLooseness.hpp"
#include "NewsGate.hpp"
#include "VolatilityHold.hpp"
#include "../session/SessionDetector.hpp"
#include <string>
#include <cstdint>
#include <ctime>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// POSITION STATE (memory for each trade)
// ═══════════════════════════════════════════════════════════════════════════════

struct PositionState {
    std::string symbol;
    int side = 0;               // +1 long / -1 short
    double entry = 0.0;
    double stop = 0.0;
    double open_r = 0.0;        // Current R-multiple
    bool risk_free = false;     // Stop moved to breakeven?
    bool scaled = false;        // Already scaled up?
    uint64_t entry_ns = 0;      // Entry timestamp (nanoseconds)
    double entry_edge = 0.0;    // Edge at entry
    double entry_spread = 0.0;  // Spread at entry
    SessionType entry_session = SessionType::OFF;
};

// ═══════════════════════════════════════════════════════════════════════════════
// EXIT DECISION - FINAL FORM
// ═══════════════════════════════════════════════════════════════════════════════
// Integrates:
// - Per-symbol exit profiles
// - Session-specific tolerance
// - News-aware hold extensions
// - Volatility-adaptive patience
// ═══════════════════════════════════════════════════════════════════════════════

struct ExitDecision {
    bool should_exit = false;
    std::string reason;
    double r_at_exit = 0.0;
};

inline ExitDecision evaluate_exit(
    const PositionState& pos,
    double edge,
    double atr_ratio,
    SessionType session,
    uint64_t now_ns,
    std::time_t utc_now
) {
    ExitDecision result;
    result.r_at_exit = pos.open_r;
    
    // Get symbol-specific exit profile
    ExitProfile prof = exit_profile(pos.symbol, session);
    
    double r = pos.open_r;
    uint64_t held_ms = (now_ns - pos.entry_ns) / 1'000'000ULL;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // RULE 1: WINNERS ARE SACRED - NEVER EXIT ABOVE PROTECTION THRESHOLD
    // ═══════════════════════════════════════════════════════════════════════════
    if (r >= prof.winner_protect_r) {
        result.should_exit = false;
        result.reason = "PROTECTED_WINNER";
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // RULE 2: MINIMUM HOLD TIME (adjusted for volatility)
    // ═══════════════════════════════════════════════════════════════════════════
    uint64_t min_hold = static_cast<uint64_t>(prof.min_hold_ms * hold_time_multiplier(atr_ratio));
    if (held_ms < min_hold) {
        result.should_exit = false;
        result.reason = "MIN_HOLD_NOT_MET";
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // RULE 3: CALCULATE REQUIRED R TO EXIT
    // ═══════════════════════════════════════════════════════════════════════════
    double required_r = 0.0;
    
    // News extension: require more R to exit during news
    if (high_impact_news_near(utc_now)) {
        required_r += news_exit_extension();
    }
    
    // Volatility extension: big moves deserve patience
    required_r += hold_extension_r(atr_ratio);
    
    // If we're above required R, don't exit
    if (r >= required_r) {
        result.should_exit = false;
        result.reason = "ABOVE_REQUIRED_R";
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // RULE 4: EDGE TOLERANCE (session-specific)
    // ═══════════════════════════════════════════════════════════════════════════
    // Only exit if edge has degraded significantly
    if (edge > prof.exit_edge_tol) {
        result.should_exit = false;
        result.reason = "EDGE_ACCEPTABLE";
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // ALL CHECKS FAILED - THIS IS A FAILED IMPULSE, EXIT
    // ═══════════════════════════════════════════════════════════════════════════
    result.should_exit = true;
    result.reason = "FAILED_IMPULSE";
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLIFIED INTERFACE
// ═══════════════════════════════════════════════════════════════════════════════

inline bool should_exit(
    const PositionState& pos,
    double edge,
    double atr_ratio,
    uint64_t now_ns
) {
    SessionType session = current_session_type();
    std::time_t utc = std::time(nullptr);
    
    ExitDecision decision = evaluate_exit(pos, edge, atr_ratio, session, now_ns, utc);
    return decision.should_exit;
}

// ═══════════════════════════════════════════════════════════════════════════════
// STOP MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════════

inline bool should_move_to_breakeven(const PositionState& pos) {
    if (pos.risk_free) return false;  // Already done
    
    ExitProfile prof = exit_profile(pos.symbol, current_session_type());
    return pos.open_r >= prof.breakeven_r;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SCALE LOGIC
// ═══════════════════════════════════════════════════════════════════════════════
// Single add, paid by market
// ═══════════════════════════════════════════════════════════════════════════════

inline bool should_scale(const PositionState& pos, SessionType session) {
    // Never scale if already scaled
    if (pos.scaled) return false;
    
    // Must be risk-free first (stop at breakeven)
    if (!pos.risk_free) return false;
    
    // Never scale during Asia
    if (session == SessionType::ASIA) return false;
    
    // Never scale during news
    if (news_blocks_scaling(std::time(nullptr))) return false;
    
    // Must meet symbol-specific threshold
    ExitProfile prof = exit_profile(pos.symbol, session);
    return pos.open_r >= prof.scale_r;
}

inline bool should_scale(const PositionState& pos) {
    return should_scale(pos, current_session_type());
}

}  // namespace Alpha
