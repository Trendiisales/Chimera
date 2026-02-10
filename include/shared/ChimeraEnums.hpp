// =============================================================================
// ChimeraEnums.hpp - v4.8.0 - UNIFIED ENUM DEFINITIONS
// =============================================================================
// PURPOSE: Single source of truth for all shared enums across Chimera
//
// This file prevents multiple definition errors by centralizing all enums
// that are used across multiple headers. Include this file instead of
// defining enums locally.
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// DO NOT DUPLICATE ENUMS ELSEWHERE
// =============================================================================
#pragma once

#include <cstdint>

namespace Chimera {

// =============================================================================
// INTENT STATES - Execution permission state machine
// =============================================================================
enum class IntentState : uint8_t {
    NO_TRADE    = 0,   // Engine idle / warming / uncertain
    WAIT_EDGE   = 1,   // Watching, gathering structure
    ARMED       = 2,   // Conditions nearly met
    LIVE        = 3,   // Edge + regime + risk aligned → EXECUTION ALLOWED
    EXIT_ONLY   = 4    // Risk event / session end → only exits allowed
};

inline const char* intent_state_str(IntentState s) {
    switch (s) {
        case IntentState::NO_TRADE:  return "NO_TRADE";
        case IntentState::WAIT_EDGE: return "WAIT_EDGE";
        case IntentState::ARMED:     return "ARMED";
        case IntentState::LIVE:      return "LIVE";
        case IntentState::EXIT_ONLY: return "EXIT_ONLY";
        default:                     return "UNKNOWN";
    }
}

// =============================================================================
// SYMBOL TYPE - Venue/instrument classification
// =============================================================================
// v4.11.0: CRYPTO REMOVED - CFD only
enum class SymbolType : uint8_t {
    CFD_FOREX = 1,
    CFD_METAL = 2,
    CFD_INDEX = 3
};

inline const char* symbol_type_str(SymbolType t) {
    switch (t) {
        case SymbolType::CFD_FOREX: return "CFD_FOREX";
        case SymbolType::CFD_METAL: return "CFD_METAL";
        case SymbolType::CFD_INDEX: return "CFD_INDEX";
        default:                    return "UNKNOWN";
    }
}

// =============================================================================
// TRADE OUTCOME - What happened to a trade opportunity
// =============================================================================
enum class TradeOutcome : uint8_t {
    EXECUTED   = 0,   // Order sent successfully
    BLOCKED    = 1,   // Good edge, but a rule stopped it (gate working correctly)
    SUPPRESSED = 2,   // Engine intentionally asleep (session/policy)
    MISSED     = 3    // Engine late / threshold too strict (opportunity lost)
};

inline const char* trade_outcome_str(TradeOutcome o) {
    switch (o) {
        case TradeOutcome::EXECUTED:   return "EXECUTED";
        case TradeOutcome::BLOCKED:    return "BLOCKED";
        case TradeOutcome::SUPPRESSED: return "SUPPRESSED";
        case TradeOutcome::MISSED:     return "MISSED";
        default:                       return "UNKNOWN";
    }
}

// =============================================================================
// BLOCK REASON - Why was the trade blocked/suppressed (UNIFIED)
// =============================================================================
enum class BlockReason : uint8_t {
    // Core execution blocks
    NONE                = 0,
    INTENT_NOT_LIVE     = 1,   // Intent != LIVE
    SYMBOL_DISABLED     = 2,   // Symbol not allowed pre-FIX
    SESSION_POLICY      = 3,   // Outside allowed session window
    NY_NOT_EXPANDED     = 4,   // NY session but no expansion detected
    REGIME_TRANSITION   = 5,   // Regime unstable
    EDGE_BELOW_THRESH   = 6,   // Edge < minimum threshold
    CONVICTION_LOW      = 7,   // Votes/conviction insufficient
    ML_VETO             = 8,   // ML gate vetoed
    RISK_LIMIT          = 9,   // Risk governor blocked
    SPREAD_TOO_WIDE     = 10,  // Spread exceeded maximum
    SPREAD_TOO_THIN     = 11,  // Spread suspiciously thin (trap)
    FIX_NOT_CONNECTED   = 12,  // Venue disconnected
    COOLDOWN            = 13,  // Rate limiting
    MAX_POSITION        = 14,  // Position limit reached
    DAILY_LOSS          = 15,  // Daily loss limit hit
    ENGINE_STANDBY      = 16,  // Engine in standby mode
    PROBE_DISABLED      = 17,  // Probes disabled for this symbol type
    EDGE_DECAYED        = 18,  // Edge was there but decayed (MISSED)
    NEG_EXPECTANCY      = 19,  // Negative expectancy
    WARMUP              = 20,  // Still warming up
    FEED_STALE          = 21,  // Data feed stale
    // Legacy aliases (for backward compatibility - use canonical names above)
    NO_BURST            = 22,  // Not in burst window 
    LOW_EDGE            = 23,  // Legacy: use EDGE_BELOW_THRESH
    SPREAD_WIDE         = 24,  // Legacy: use SPREAD_TOO_WIDE
    POSITION_OPEN       = 25,  // Legacy: use MAX_POSITION
    SESSION_CLOSED      = 26,  // Legacy: use SESSION_POLICY
    OTHER               = 99   // Other reason
};

inline const char* block_reason_str(BlockReason r) {
    switch (r) {
        case BlockReason::NONE:              return "NONE";
        case BlockReason::INTENT_NOT_LIVE:   return "INTENT_NOT_LIVE";
        case BlockReason::SYMBOL_DISABLED:   return "SYMBOL_DISABLED";
        case BlockReason::SESSION_POLICY:    return "SESSION_POLICY";
        case BlockReason::NY_NOT_EXPANDED:   return "NY_NOT_EXPANDED";
        case BlockReason::REGIME_TRANSITION: return "REGIME_TRANSITION";
        case BlockReason::EDGE_BELOW_THRESH: return "EDGE_BELOW_THRESH";
        case BlockReason::CONVICTION_LOW:    return "CONVICTION_LOW";
        case BlockReason::ML_VETO:           return "ML_VETO";
        case BlockReason::RISK_LIMIT:        return "RISK_LIMIT";
        case BlockReason::SPREAD_TOO_WIDE:   return "SPREAD_TOO_WIDE";
        case BlockReason::SPREAD_TOO_THIN:   return "SPREAD_TOO_THIN";
        case BlockReason::FIX_NOT_CONNECTED: return "FIX_NOT_CONNECTED";
        case BlockReason::COOLDOWN:          return "COOLDOWN";
        case BlockReason::MAX_POSITION:      return "MAX_POSITION";
        case BlockReason::DAILY_LOSS:        return "DAILY_LOSS";
        case BlockReason::ENGINE_STANDBY:    return "ENGINE_STANDBY";
        case BlockReason::PROBE_DISABLED:    return "PROBE_DISABLED";
        case BlockReason::EDGE_DECAYED:      return "EDGE_DECAYED";
        case BlockReason::NEG_EXPECTANCY:    return "NEG_EXPECTANCY";
        case BlockReason::WARMUP:            return "WARMUP";
        case BlockReason::FEED_STALE:        return "FEED_STALE";
        case BlockReason::NO_BURST:          return "NO_BURST";
        case BlockReason::LOW_EDGE:          return "LOW_EDGE";
        case BlockReason::SPREAD_WIDE:       return "SPREAD_WIDE";
        case BlockReason::POSITION_OPEN:     return "POSITION_OPEN";
        case BlockReason::SESSION_CLOSED:    return "SESSION_CLOSED";
        case BlockReason::OTHER:             return "OTHER";
        default:                             return "UNKNOWN";
    }
}

// =============================================================================
// LATENCY STATE - Network/execution latency classification
// =============================================================================
enum class LatencyState : uint8_t {
    NORMAL   = 0,
    ELEVATED = 1,
    DEGRADED = 2
};

inline const char* latency_state_str(LatencyState s) {
    switch (s) {
        case LatencyState::NORMAL:   return "NORMAL";
        case LatencyState::ELEVATED: return "ELEVATED";
        case LatencyState::DEGRADED: return "DEGRADED";
        default:                     return "UNKNOWN";
    }
}

// =============================================================================
// SHOCK STATE - Market shock detection state
// =============================================================================
enum class ShockState : uint8_t {
    CLEAR    = 0,
    DETECTED = 1,
    COOLDOWN = 2
};

inline const char* shock_state_str(ShockState s) {
    switch (s) {
        case ShockState::CLEAR:    return "CLEAR";
        case ShockState::DETECTED: return "DETECTED";
        case ShockState::COOLDOWN: return "COOLDOWN";
        default:                   return "UNKNOWN";
    }
}

// =============================================================================
// REGIME STATE - Market regime classification
// =============================================================================
enum class RegimeState : uint8_t {
    RANGING  = 0,
    BREAKOUT = 1,
    TRENDING = 2,
    TOXIC    = 3
};

inline const char* regime_state_str(RegimeState r) {
    switch (r) {
        case RegimeState::RANGING:  return "RANGING";
        case RegimeState::BREAKOUT: return "BREAKOUT";
        case RegimeState::TRENDING: return "TRENDING";
        case RegimeState::TOXIC:    return "TOXIC";
        default:                    return "UNKNOWN";
    }
}

} // namespace Chimera
