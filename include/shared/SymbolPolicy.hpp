// =============================================================================
// SymbolPolicy.hpp - v4.11.0 - CFD SYMBOL RULES
// =============================================================================
// PURPOSE: Defines which symbols are allowed and under what conditions
// v4.11.0: CRYPTO REMOVED - CFD only (cTrader via FIX or OpenAPI)
//
// ALLOWED SYMBOLS:
//   CFD METALS: XAUUSD (NY session, validated)
//   CFD INDEX:  NAS100, US30 (NY session, micro-live validated)
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-06
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "IntentGate.hpp"

namespace Chimera {

// =============================================================================
// Session Windows
// =============================================================================
enum class SessionWindow : uint8_t {
    ANY           = 0,   // Any time
    NY            = 1,   // NY session (13:30-20:00 UTC)
    NY_EXPANSION  = 2,   // NY session + expansion detected
    LONDON        = 3,   // London session (07:00-16:00 UTC)
    LONDON_NY     = 4,   // London-NY overlap (13:30-16:00 UTC)
    ASIA          = 5    // Asia session (00:00-07:00 UTC)
};

inline const char* session_window_str(SessionWindow s) {
    switch (s) {
        case SessionWindow::ANY:          return "ANY";
        case SessionWindow::NY:           return "NY";
        case SessionWindow::NY_EXPANSION: return "NY_EXPANSION";
        case SessionWindow::LONDON:       return "LONDON";
        case SessionWindow::LONDON_NY:    return "LONDON_NY";
        case SessionWindow::ASIA:         return "ASIA";
        default:                          return "UNKNOWN";
    }
}

// =============================================================================
// Symbol Policy (per-symbol trading rules)
// =============================================================================
struct SymbolPolicy {
    bool allowed;              // Is trading allowed at all?
    SymbolType type;           // Symbol classification
    SessionWindow session;     // Required session window
    double min_spread_bps;     // Minimum spread (reject if thinner - trap)
    double max_spread_bps;     // Maximum spread (reject if wider)
    bool probes_allowed;       // Are probe trades allowed?
    bool shadow_allowed;       // Is shadow trading allowed?
    int tier;                  // Priority tier (1=primary, 2=secondary, 3=sensor, 4=disabled)
    
    SymbolPolicy() 
        : allowed(false), type(SymbolType::CFD_INDEX), session(SessionWindow::ANY),
          min_spread_bps(0.0), max_spread_bps(100.0), probes_allowed(false),
          shadow_allowed(false), tier(4) {}
          
    SymbolPolicy(bool a, SymbolType t, SessionWindow s, double minSpread, double maxSpread,
                 bool probes, bool shadow, int ti)
        : allowed(a), type(t), session(s), min_spread_bps(minSpread),
          max_spread_bps(maxSpread), probes_allowed(probes), shadow_allowed(shadow),
          tier(ti) {}
};

// =============================================================================
// Gold-Specific Policy (CFD Safe Mode)
// =============================================================================
struct GoldPolicy {
    bool allow = true;
    double min_spread_bps = 0.25;      // Reject thin spreads (trap detection)
    double max_spread_bps = 5.0;       // Reject wide spreads
    IntentState min_intent = IntentState::LIVE;
    bool continuation_only = true;     // Only continuation trades, no fading
    int max_positions = 1;             // One position max
    bool reversals_allowed = false;    // No flip-flopping
};

// =============================================================================
// PRE-FIX POLICY TABLE
// =============================================================================
// This is the SINGLE SOURCE OF TRUTH for what Chimera can trade pre-FIX.
// Changing this table is the ONLY way to enable/disable symbols.
// v4.11.0: CRYPTO REMOVED - CFD only

inline const std::unordered_map<std::string, SymbolPolicy>& getPreFIXPolicy() {
    static const std::unordered_map<std::string, SymbolPolicy> policy = {
        // ═════════════════════════════════════════════════════════════════════
        // CFD METALS (GOLD ONLY - NY SESSION)
        // ═════════════════════════════════════════════════════════════════════
        {"XAUUSD", {
            true,                       // allowed
            SymbolType::CFD_METAL,      // type
            SessionWindow::NY_EXPANSION,// session (NY expansion only)
            0.25,                       // min_spread_bps (trap detection)
            5.0,                        // max_spread_bps
            false,                      // probes_allowed (NO - spread tax)
            true,                       // shadow_allowed (learn from it)
            2                           // tier 2
        }},
        {"XAGUSD", {
            false,                      // DISABLED pre-FIX
            SymbolType::CFD_METAL,
            SessionWindow::NY,
            0.5, 12.0,
            false, true, 3              // tier 3 - sensor only
        }},
        
        // ═════════════════════════════════════════════════════════════════════
        // CFD INDICES (ALL DISABLED PRE-FIX)
        // ═════════════════════════════════════════════════════════════════════
        {"NAS100", {
            false,                      // DISABLED - spread flickers
            SymbolType::CFD_INDEX,
            SessionWindow::NY,
            0.5, 4.0,
            false, true, 3              // sensor only
        }},
        {"US100", {
            false,                      // DISABLED (NAS100 alias)
            SymbolType::CFD_INDEX,
            SessionWindow::NY,
            0.5, 4.0,
            false, true, 3
        }},
        {"US30", {
            false,                      // DISABLED - similar issues
            SymbolType::CFD_INDEX,
            SessionWindow::NY,
            0.5, 6.0,
            false, true, 3
        }},
        {"SPX500", {
            false,                      // DISABLED
            SymbolType::CFD_INDEX,
            SessionWindow::NY,
            0.5, 4.0,
            false, true, 3
        }},
        {"GER40", {
            false,                      // DISABLED
            SymbolType::CFD_INDEX,
            SessionWindow::LONDON,
            0.5, 6.0,
            false, true, 3
        }},
        
        // ═════════════════════════════════════════════════════════════════════
        // CFD FOREX (DISABLED PRE-FIX for focus)
        // ═════════════════════════════════════════════════════════════════════
        {"EURUSD", {
            false,                      // DISABLED for focus
            SymbolType::CFD_FOREX,
            SessionWindow::LONDON_NY,
            0.1, 2.0,
            false, true, 3
        }},
        {"GBPUSD", {
            false,
            SymbolType::CFD_FOREX,
            SessionWindow::LONDON_NY,
            0.2, 3.0,
            false, true, 3
        }},
        {"USDJPY", {
            false,
            SymbolType::CFD_FOREX,
            SessionWindow::ASIA,
            0.2, 2.5,
            false, true, 4
        }},
        {"AUDUSD", {
            false,
            SymbolType::CFD_FOREX,
            SessionWindow::ASIA,
            0.3, 3.0,
            false, true, 4
        }},
        {"USDCAD", {false, SymbolType::CFD_FOREX, SessionWindow::NY, 0.3, 3.0, false, false, 4}},
        {"USDCHF", {false, SymbolType::CFD_FOREX, SessionWindow::LONDON, 0.3, 3.0, false, false, 4}},
        {"NZDUSD", {false, SymbolType::CFD_FOREX, SessionWindow::ASIA, 0.4, 4.0, false, false, 4}},
        {"EURGBP", {false, SymbolType::CFD_FOREX, SessionWindow::LONDON, 0.3, 3.0, false, false, 4}}
    };
    return policy;
}

// =============================================================================
// GOLD SAFE MODE POLICY
// =============================================================================
inline const GoldPolicy& getGoldPolicy() {
    static const GoldPolicy policy;
    return policy;
}

// =============================================================================
// POLICY CHECKER
// =============================================================================
class SymbolPolicyChecker {
public:
    struct CheckResult {
        bool allowed;
        BlockReason reason;
        const SymbolPolicy* policy;
    };
    
    // =========================================================================
    // CHECK SYMBOL ALLOWED
    // =========================================================================
    [[nodiscard]] static CheckResult checkSymbol(
        const char* symbol,
        SessionWindow current_session,
        double spread_bps,
        bool ny_expansion_active
    ) {
        CheckResult result;
        result.allowed = false;
        result.reason = BlockReason::NONE;
        result.policy = nullptr;
        
        // Look up policy
        const auto& policy_map = getPreFIXPolicy();
        auto it = policy_map.find(symbol);
        
        if (it == policy_map.end()) {
            // Unknown symbol - not allowed
            result.reason = BlockReason::SYMBOL_DISABLED;
            return result;
        }
        
        const SymbolPolicy& pol = it->second;
        result.policy = &pol;
        
        // Check 1: Is symbol allowed at all?
        if (!pol.allowed) {
            result.reason = BlockReason::SYMBOL_DISABLED;
            return result;
        }
        
        // Check 2: Session window
        if (!isSessionSatisfied(pol.session, current_session, ny_expansion_active)) {
            result.reason = BlockReason::SESSION_POLICY;
            return result;
        }
        
        // Check 3: Spread bounds
        if (spread_bps < pol.min_spread_bps) {
            result.reason = BlockReason::SPREAD_TOO_THIN;
            return result;
        }
        if (spread_bps > pol.max_spread_bps) {
            result.reason = BlockReason::SPREAD_TOO_WIDE;
            return result;
        }
        
        // All checks passed
        result.allowed = true;
        return result;
    }
    
    // =========================================================================
    // CHECK GOLD-SPECIFIC RULES
    // =========================================================================
    [[nodiscard]] static CheckResult checkGold(
        IntentState intent,
        double spread_bps,
        bool is_continuation,
        int current_positions
    ) {
        CheckResult result;
        result.allowed = false;
        result.reason = BlockReason::NONE;
        result.policy = nullptr;
        
        const GoldPolicy& gp = getGoldPolicy();
        
        if (!gp.allow) {
            result.reason = BlockReason::SYMBOL_DISABLED;
            return result;
        }
        
        // Intent check
        if (static_cast<uint8_t>(intent) < static_cast<uint8_t>(gp.min_intent)) {
            result.reason = BlockReason::INTENT_NOT_LIVE;
            return result;
        }
        
        // Spread check (thin = trap)
        if (spread_bps < gp.min_spread_bps) {
            result.reason = BlockReason::SPREAD_TOO_THIN;
            return result;
        }
        if (spread_bps > gp.max_spread_bps) {
            result.reason = BlockReason::SPREAD_TOO_WIDE;
            return result;
        }
        
        // Continuation only
        if (gp.continuation_only && !is_continuation) {
            result.reason = BlockReason::ML_VETO;  // Re-use for "not continuation"
            return result;
        }
        
        // Position limit
        if (current_positions >= gp.max_positions) {
            result.reason = BlockReason::MAX_POSITION;
            return result;
        }
        
        result.allowed = true;
        return result;
    }
    
    // =========================================================================
    // GET SYMBOL TYPE
    // =========================================================================
    [[nodiscard]] static SymbolType getSymbolType(const char* symbol) {
        const auto& policy_map = getPreFIXPolicy();
        auto it = policy_map.find(symbol);
        if (it != policy_map.end()) {
            return it->second.type;
        }
        // Default to CFD index (most restrictive)
        return SymbolType::CFD_INDEX;
    }

private:
    static bool isSessionSatisfied(
        SessionWindow required,
        SessionWindow current,
        bool ny_expansion_active
    ) {
        if (required == SessionWindow::ANY) return true;
        
        // NY_EXPANSION requires both NY session and expansion
        if (required == SessionWindow::NY_EXPANSION) {
            return (current == SessionWindow::NY || current == SessionWindow::NY_EXPANSION) 
                   && ny_expansion_active;
        }
        
        // Direct match
        if (required == current) return true;
        
        // LONDON_NY satisfies both LONDON and NY
        if (required == SessionWindow::LONDON_NY) {
            return current == SessionWindow::LONDON || 
                   current == SessionWindow::NY ||
                   current == SessionWindow::LONDON_NY;
        }
        
        return false;
    }
};

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

[[nodiscard]] inline bool isSymbolAllowed(
    const char* symbol,
    SessionWindow current_session,
    double spread_bps,
    bool ny_expansion_active = false
) {
    auto result = SymbolPolicyChecker::checkSymbol(symbol, current_session, 
                                                    spread_bps, ny_expansion_active);
    return result.allowed;
}

[[nodiscard]] inline SymbolType getSymbolType(const char* symbol) {
    return SymbolPolicyChecker::getSymbolType(symbol);
}

[[nodiscard]] inline const SymbolPolicy* getSymbolPolicy(const char* symbol) {
    const auto& policy_map = getPreFIXPolicy();
    auto it = policy_map.find(symbol);
    return (it != policy_map.end()) ? &it->second : nullptr;
}

} // namespace Chimera
