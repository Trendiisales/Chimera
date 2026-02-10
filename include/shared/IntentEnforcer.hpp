// =============================================================================
// IntentEnforcer.hpp - v4.8.0 - UNIFIED EXECUTION ENFORCEMENT
// =============================================================================
// PURPOSE: Single-call wrapper for all intent-based execution checks
//
// v4.8.0 CHANGES:
//   - Replaced hardcoded regime_stable=true with actual regime state
//   - Added regime stability tracking per symbol
//
// USAGE:
//   auto result = IntentEnforcer::check(engine_id, symbol, spread_bps, now_ns);
//   if (!result.allowed) {
//       // Log result.outcome, result.reason
//       return;
//   }
//   // Proceed with execution
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// =============================================================================
#pragma once

#include "IntentGate.hpp"
#include "SymbolPolicy.hpp"
#include "SessionDetector.hpp"
#include "ExecutionReplay.hpp"
#include "GlobalRiskGovernor.hpp"

namespace Chimera {

// =============================================================================
// v4.8.0: REGIME STABILITY TRACKER (per-symbol)
// =============================================================================
class RegimeStabilityTracker {
public:
    static RegimeStabilityTracker& instance() {
        static RegimeStabilityTracker inst;
        return inst;
    }
    
    // Update regime stability for a symbol
    void update(const char* symbol, bool stable) {
        size_t idx = symbolIndex(symbol);
        if (idx < MAX_SYMBOLS) {
            stability_[idx].store(stable, std::memory_order_release);
        }
    }
    
    // Check if regime is stable for a symbol
    bool isStable(const char* symbol) const {
        size_t idx = symbolIndex(symbol);
        if (idx < MAX_SYMBOLS) {
            return stability_[idx].load(std::memory_order_acquire);
        }
        return true;  // Default to stable if unknown
    }

private:
    static constexpr size_t MAX_SYMBOLS = 16;
    
    RegimeStabilityTracker() {
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            stability_[i].store(true, std::memory_order_relaxed);
        }
    }
    
    size_t symbolIndex(const char* symbol) const {
        // Simple hash-based index
        if (!symbol) return MAX_SYMBOLS;
        
        // Known symbols get fixed indices
        if (strcmp(symbol, "NAS100") == 0) return 0;
        if (strcmp(symbol, "XAUUSD") == 0) return 1;
        if (strcmp(symbol, "EURUSD") == 0) return 2;
        if (strcmp(symbol, "GBPUSD") == 0) return 3;
        if (strcmp(symbol, "USDJPY") == 0) return 4;
        if (strcmp(symbol, "US30") == 0)   return 5;
        if (strcmp(symbol, "SPX500") == 0) return 6;
        if (strcmp(symbol, "XAGUSD") == 0) return 7;
        // v4.11.0: crypto symbols removed (indices 8-9 reserved)
        
        // Unknown symbols get remainder-based index
        size_t hash = 0;
        for (const char* p = symbol; *p; ++p) {
            hash = hash * 31 + *p;
        }
        return 10 + (hash % 6);  // Use indices 10-15 for unknown
    }
    
    std::atomic<bool> stability_[MAX_SYMBOLS];
};

inline RegimeStabilityTracker& getRegimeStabilityTracker() {
    return RegimeStabilityTracker::instance();
}

// =============================================================================
// Intent Enforcer - THE execution gatekeeper
// =============================================================================
class IntentEnforcer {
public:
    // =========================================================================
    // CHECK RESULT
    // =========================================================================
    struct CheckResult {
        bool allowed;
        TradeOutcome outcome;
        BlockReason reason;
        IntentState intent;
        double edge;
        double conviction;
        bool regime_stable;  // v4.8.0: Now tracked properly
        
        CheckResult() 
            : allowed(false), outcome(TradeOutcome::SUPPRESSED), 
              reason(BlockReason::NONE), intent(IntentState::NO_TRADE),
              edge(0.0), conviction(0.0), regime_stable(true) {}
    };
    
    // =========================================================================
    // THE SINGLE EXECUTION CHECK
    // Call this at the execution boundary BEFORE any order is submitted
    // =========================================================================
    [[nodiscard]] static CheckResult check(
        EngineId engine_id,
        const char* symbol,
        double spread_bps,
        uint64_t now_ns,
        bool is_exit_order = false
    ) {
        CheckResult result;
        
        // =====================================================================
        // CHECK 0: ENGINE STANDBY MODE
        // =====================================================================
        if (getIntentGate().isStandby()) {
            result.reason = BlockReason::ENGINE_STANDBY;
            result.outcome = TradeOutcome::SUPPRESSED;
            logDecision(symbol, result, spread_bps, now_ns);
            return result;
        }
        
        // =====================================================================
        // CHECK 1: INTENT STATE (THE PRIME DIRECTIVE)
        // NO ORDER MAY BE SENT UNLESS INTENT == LIVE
        // =====================================================================
        result.intent = getIntentGate().getIntent(symbol);
        SymbolType sym_type = SymbolPolicyChecker::getSymbolType(symbol);
        
        auto intent_result = getIntentGate().checkExecution(symbol, sym_type, is_exit_order);
        if (!intent_result.allowed) {
            result.reason = intent_result.reason;
            result.outcome = intent_result.outcome;
            logDecision(symbol, result, spread_bps, now_ns);
            return result;
        }
        
        // =====================================================================
        // CHECK 2: SYMBOL POLICY (PRE-FIX RULES)
        // =====================================================================
        SessionWindow current_session = getSessionDetector().currentWindow();
        bool ny_expanded = getSessionDetector().isSymbolExpanded(symbol);
        
        auto policy_result = SymbolPolicyChecker::checkSymbol(
            symbol, current_session, spread_bps, ny_expanded);
        
        if (!policy_result.allowed) {
            result.reason = policy_result.reason;
            result.outcome = TradeOutcome::BLOCKED;
            logDecision(symbol, result, spread_bps, now_ns);
            return result;
        }
        
        // =====================================================================
        // CHECK 3: RISK GOVERNOR
        // =====================================================================
        if (!GlobalRiskGovernor::instance().canSubmitOrder(engine_id)) {
            result.reason = BlockReason::RISK_LIMIT;
            result.outcome = TradeOutcome::BLOCKED;
            logDecision(symbol, result, spread_bps, now_ns);
            return result;
        }
        
        // =====================================================================
        // CHECK 4: ENGINE OWNERSHIP
        // =====================================================================
            result.reason = BlockReason::SYMBOL_DISABLED;
            result.outcome = TradeOutcome::BLOCKED;
            logDecision(symbol, result, spread_bps, now_ns);
            return result;
        }
        
        // =====================================================================
        // ALL CHECKS PASSED - EXECUTION ALLOWED
        // =====================================================================
        result.allowed = true;
        result.outcome = TradeOutcome::EXECUTED;
        result.reason = BlockReason::NONE;
        
        // Get metrics from intent gate
        auto* sym_intent = const_cast<IntentGate&>(getIntentGate()).getSymbolIntent(symbol);
        if (sym_intent) {
            result.edge = sym_intent->current_edge.load();
            result.conviction = sym_intent->current_conviction.load();
        }
        
        // v4.8.0: Get actual regime stability
        result.regime_stable = getRegimeStabilityTracker().isStable(symbol);
        
        // Log executed trade
        getReplayLogger().logExecuted(symbol, result.intent, result.edge, 
                                       result.conviction, spread_bps, now_ns);
        
        return result;
    }
    
    // =========================================================================
    // UPDATE INTENT STATE (call on each tick/signal)
    // =========================================================================
    static IntentState updateIntent(
        const char* symbol,
        double edge,
        double conviction,
        bool regime_stable,
        uint64_t now_ns
    ) {
        // v4.8.0: Store regime stability for later use
        getRegimeStabilityTracker().update(symbol, regime_stable);
        
        // Update session detector first
        getSessionDetector().updateSession(now_ns);
        bool session_ok = getSessionDetector().isCoreSession();
        
        // Record edge for standby detection
        getSessionDetector().recordEdge(symbol, edge);
        
        // Update intent state machine
        return getIntentGate().updateIntent(
            symbol, edge, conviction, regime_stable, session_ok, now_ns);
    }
    
    // =========================================================================
    // UPDATE EXPANSION METRICS (call on each tick)
    // =========================================================================
    static void updateSessionMetrics(
        const char* symbol,
        double price,
        double bid_size,
        double ask_size,
        uint64_t now_ns
    ) {
        getSessionDetector().updateMetrics(symbol, price, bid_size, ask_size, now_ns);
    }
    
    // =========================================================================
    // v4.8.0: GET REGIME STABILITY
    // =========================================================================
    [[nodiscard]] static bool isRegimeStable(const char* symbol) {
        return getRegimeStabilityTracker().isStable(symbol);
    }
    
    // =========================================================================
    // CHECK IF SHADOW TRADING ALLOWED
    // Shadow trading has different rules - more permissive
    // =========================================================================
    [[nodiscard]] static bool canShadowTrade(const char* symbol, double spread_bps) {
        // Look up policy
        const SymbolPolicy* policy = getSymbolPolicy(symbol);
        if (!policy) return false;
        
        // Shadow must be explicitly allowed
        if (!policy->shadow_allowed) return false;
        
        // Check spread bounds (more permissive for shadow)
        double shadow_max_spread = policy->max_spread_bps * 1.5;  // 50% wider for shadow
        if (spread_bps > shadow_max_spread) return false;
        
        return true;
    }
    
    // =========================================================================
    // CHECK IF PROBES ALLOWED
    // =========================================================================
    [[nodiscard]] static bool canProbe(const char* symbol) {
        const SymbolPolicy* policy = getSymbolPolicy(symbol);
        if (!policy) return false;
        return policy->probes_allowed;
    }
    
    // =========================================================================
    // STANDBY MANAGEMENT
    // =========================================================================
    static bool shouldEnterStandby(uint64_t now_ns) {
        return getSessionDetector().shouldStandby(now_ns);
    }
    
    static void enterStandby(uint64_t now_ns) {
        getIntentGate().enterStandby(now_ns);
    }
    
    static void exitStandby() {
        getIntentGate().exitStandby();
    }
    
    static bool isStandby() {
        return getIntentGate().isStandby();
    }
    
    // =========================================================================
    // SESSION QUERIES
    // =========================================================================
    [[nodiscard]] static bool isNYSession() {
        return getSessionDetector().isNYSession();
    }
    
    [[nodiscard]] static bool isNYExpanded(const char* symbol) {
        return getSessionDetector().isSymbolExpanded(symbol);
    }
    
    [[nodiscard]] static SessionWindow currentSession() {
        return getSessionDetector().currentWindow();
    }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    static void printStatus() {
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("INTENT ENFORCER STATUS (v4.8.0)\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        getIntentGate().printStatus();
        getSessionDetector().printStatus();
        getReplayLogger().printSessionSummary();
        
        // v4.8.0: Print regime stability status
        printf("\n  Regime Stability:\n");
        const char* symbols[] = {"NAS100", "XAUUSD", "EURUSD", "GBPUSD", "USDJPY"};
        for (const char* sym : symbols) {
            bool stable = getRegimeStabilityTracker().isStable(sym);
            printf("    %s: %s\n", sym, stable ? "STABLE" : "UNSTABLE");
        }
    }
    
    static void resetSession() {
        getReplayLogger().resetSession();
    }

private:
    static void logDecision(
        const char* symbol,
        const CheckResult& result,
        double spread_bps,
        uint64_t now_ns
    ) {
        // Build failing gates string
        char failing_gates[64] = "";
        if (result.reason != BlockReason::NONE) {
            snprintf(failing_gates, sizeof(failing_gates), "%s", 
                     block_reason_str(result.reason));
        }
        
        // v4.8.0: Get actual regime stability
        bool regime_stable = getRegimeStabilityTracker().isStable(symbol);
        
        // Log to replay
        getReplayLogger().logSnapshot(
            symbol,
            result.intent,
            result.edge,
            result.conviction,
            spread_bps,
            getSessionDetector().isSymbolExpanded(symbol),
            regime_stable,  // v4.8.0: Use actual value
            getSessionDetector().isCoreSession(),
            result.reason,
            failing_gates,
            now_ns
        );
    }
};

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

[[nodiscard]] inline IntentEnforcer::CheckResult checkExecution(
    EngineId engine_id,
    const char* symbol,
    double spread_bps,
    uint64_t now_ns,
    bool is_exit_order = false
) {
    return IntentEnforcer::check(engine_id, symbol, spread_bps, now_ns, is_exit_order);
}

inline IntentState updateSymbolIntent(
    const char* symbol,
    double edge,
    double conviction,
    bool regime_stable,
    uint64_t now_ns
) {
    return IntentEnforcer::updateIntent(symbol, edge, conviction, regime_stable, now_ns);
}

// v4.8.0: Update regime stability directly
inline void updateRegimeStability(const char* symbol, bool stable) {
    getRegimeStabilityTracker().update(symbol, stable);
}

} // namespace Chimera
