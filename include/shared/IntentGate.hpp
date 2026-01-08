// =============================================================================
// IntentGate.hpp - v4.8.0 - THE PRIME DIRECTIVE
// =============================================================================
// PURPOSE: Intent-based execution gating - THE fundamental rule of Chimera
//
// ═══════════════════════════════════════════════════════════════════════════════
//                         🔒 THE PRIME DIRECTIVE 🔒
//    
//    Chimera is allowed to lose money.
//    Chimera is NOT allowed to trade without intent.
//
// ═══════════════════════════════════════════════════════════════════════════════
//
// NO ORDER MAY BE SENT UNLESS INTENT == LIVE.
//
// This is the SINGLE SOURCE OF TRUTH for execution permission.
// All other checks are subordinate to this rule.
//
// DESIGN:
//   - Per-symbol intent state machine
//   - Intent state transitions are logged and audited
//   - Probes are NOT execution - they are disabled by default for CFDs
//   - Fallback fills are NOT allowed
//   - CFD symbols do not get exceptions
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <unordered_map>
#include <string>

// Use unified enum definitions
#include "ChimeraEnums.hpp"

namespace Chimera {

// =============================================================================
// Decision Snapshot - For execution replay logging
// =============================================================================
struct DecisionSnapshot {
    uint64_t ts_ns;
    char symbol[16];
    IntentState intent;
    bool ny_expansion;
    double edge;
    double conviction;
    double spread_bps;
    TradeOutcome outcome;
    BlockReason reason;
    
    void clear() {
        ts_ns = 0;
        symbol[0] = '\0';
        intent = IntentState::NO_TRADE;
        ny_expansion = false;
        edge = 0.0;
        conviction = 0.0;
        spread_bps = 0.0;
        outcome = TradeOutcome::SUPPRESSED;
        reason = BlockReason::NONE;
    }
    
    void setSymbol(const char* sym) {
        std::strncpy(symbol, sym, 15);
        symbol[15] = '\0';
    }
};

// =============================================================================
// Execution Permission - THE ABSOLUTE RULE
// =============================================================================
// This is the root fix. Everything routes through this.
// If you do nothing else, enforce this.

[[nodiscard]] inline bool canExecute(
    IntentState intent,
    SymbolType symbol_type,
    bool is_exit_order = false
) noexcept {
    (void)symbol_type;  // Suppress unused parameter warning
    
    // EXIT_ONLY allows exits regardless of intent state
    if (is_exit_order && intent == IntentState::EXIT_ONLY) {
        return true;
    }
    
    // LIVE allows everything
    if (intent == IntentState::LIVE) {
        return true;
    }
    
    // Nothing else allowed
    // This is intentionally restrictive
    // DO NOT ADD EXCEPTIONS HERE
    return false;
}

// =============================================================================
// Symbol Intent Tracker - Per-symbol state machine
// =============================================================================
struct SymbolIntent {
    std::atomic<IntentState> state{IntentState::NO_TRADE};
    std::atomic<uint64_t> last_change_ts{0};
    std::atomic<uint32_t> state_change_count{0};
    std::atomic<double> current_edge{0.0};
    std::atomic<double> current_conviction{0.0};
    
    [[nodiscard]] IntentState get() const noexcept {
        return state.load(std::memory_order_acquire);
    }
    
    void transition(IntentState new_state, uint64_t ts_ns) noexcept {
        state.store(new_state, std::memory_order_release);
        last_change_ts.store(ts_ns, std::memory_order_release);
        state_change_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    [[nodiscard]] uint64_t lastChangeTs() const noexcept {
        return last_change_ts.load(std::memory_order_acquire);
    }
    
    void updateMetrics(double edge, double conviction) noexcept {
        current_edge.store(edge, std::memory_order_release);
        current_conviction.store(conviction, std::memory_order_release);
    }
};

// =============================================================================
// Intent Gate Configuration
// =============================================================================
struct IntentGateConfig {
    // Edge thresholds for state transitions
    double edge_wait_thresh = 0.20;      // NO_TRADE → WAIT_EDGE
    double edge_armed_thresh = 0.40;     // WAIT_EDGE → ARMED
    double edge_live_thresh = 0.55;      // ARMED → LIVE
    
    // Conviction thresholds
    double conviction_live_thresh = 0.50;
    
    // Timing
    uint64_t armed_timeout_ns = 30000000000ULL;   // 30 seconds
    uint64_t live_persist_ns = 2000000000ULL;     // 2 seconds minimum in LIVE
    
    // Probes
    bool probes_allowed_cfd = false;     // CFD probes DISABLED by default (spread tax)
    // v4.11.0: crypto removed
};

// =============================================================================
// Intent Gate - THE GATEKEEPER
// =============================================================================
class IntentGate {
public:
    using Config = IntentGateConfig;
    
    explicit IntentGate(Config config = Config{}) : config_(config) {}
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    [[nodiscard]] IntentState getIntent(const char* symbol) const {
        auto it = intents_.find(symbol);
        if (it == intents_.end()) {
            return IntentState::NO_TRADE;
        }
        return it->second.get();
    }
    
    [[nodiscard]] bool isLive(const char* symbol) const {
        return getIntent(symbol) == IntentState::LIVE;
    }
    
    // Get raw SymbolIntent pointer for metrics access (non-const for atomics)
    [[nodiscard]] SymbolIntent* getSymbolIntent(const char* symbol) {
        auto it = intents_.find(symbol);
        if (it == intents_.end()) {
            return nullptr;
        }
        return &it->second;
    }
    
    // =========================================================================
    // STATE MACHINE UPDATE
    // =========================================================================
    IntentState updateIntent(
        const char* symbol,
        double edge,
        double conviction,
        bool regime_stable,
        bool session_ok,
        uint64_t now_ns
    ) {
        // Get or create intent
        auto& intent = intents_[symbol];
        
        // Store metrics for diagnostics
        intent.updateMetrics(edge, conviction);
        
        // Get current state
        IntentState current = intent.get();
        IntentState next = current;
        
        // State machine transitions
        switch (current) {
            case IntentState::NO_TRADE:
                // Transition to WAIT_EDGE when edge appears
                if (edge >= config_.edge_wait_thresh && session_ok) {
                    next = IntentState::WAIT_EDGE;
                }
                break;
                
            case IntentState::WAIT_EDGE:
                // Transition to ARMED when edge strengthens
                if (edge >= config_.edge_armed_thresh && regime_stable) {
                    next = IntentState::ARMED;
                }
                // Fall back to NO_TRADE if edge disappears
                else if (edge < config_.edge_wait_thresh * 0.7 || !session_ok) {
                    next = IntentState::NO_TRADE;
                }
                break;
                
            case IntentState::ARMED:
                // Transition to LIVE when fully confirmed
                if (edge >= config_.edge_live_thresh && 
                    conviction >= config_.conviction_live_thresh &&
                    regime_stable && session_ok) {
                    next = IntentState::LIVE;
                }
                // Fall back if conditions deteriorate
                else if (edge < config_.edge_armed_thresh * 0.8) {
                    next = IntentState::WAIT_EDGE;
                }
                // Timeout in ARMED state
                else if (now_ns - intent.lastChangeTs() > config_.armed_timeout_ns) {
                    next = IntentState::WAIT_EDGE;
                }
                break;
                
            case IntentState::LIVE:
                // Stay LIVE for minimum persistence period
                if (now_ns - intent.lastChangeTs() < config_.live_persist_ns) {
                    next = IntentState::LIVE;
                }
                // Transition back when edge fades
                else if (edge < config_.edge_live_thresh * 0.7 ||
                         conviction < config_.conviction_live_thresh * 0.7) {
                    next = IntentState::ARMED;
                }
                // Exit-only if session ends
                else if (!session_ok) {
                    next = IntentState::EXIT_ONLY;
                }
                break;
                
            case IntentState::EXIT_ONLY:
                // Transition to NO_TRADE when session ends
                if (!session_ok) {
                    next = IntentState::NO_TRADE;
                }
                // Back to LIVE if conditions return
                else if (edge >= config_.edge_live_thresh && session_ok) {
                    next = IntentState::LIVE;
                }
                break;
        }
        
        // Apply transition
        if (next != current) {
            intent.transition(next, now_ns);
            logTransition(symbol, current, next, edge, conviction, now_ns);
        }
        
        return next;
    }
    
    // =========================================================================
    // FORCE STATE (for risk events, session end, etc)
    // =========================================================================
    void forceState(const char* symbol, IntentState new_state, uint64_t now_ns) {
        auto& intent = intents_[symbol];
        IntentState old = intent.get();
        intent.transition(new_state, now_ns);
        
        if (old != new_state) {
            printf("[INTENT-GATE] FORCED: %s %s → %s\n",
                   symbol, intent_state_str(old), intent_state_str(new_state));
        }
    }
    
    // Force all symbols to a state (e.g., session end)
    void forceAllState(IntentState new_state, uint64_t now_ns) {
        for (auto& [symbol, intent] : intents_) {
            IntentState old = intent.get();
            intent.transition(new_state, now_ns);
            
            if (old != new_state) {
                printf("[INTENT-GATE] FORCED_ALL: %s %s → %s\n",
                       symbol.c_str(), intent_state_str(old), intent_state_str(new_state));
            }
        }
    }
    
    // =========================================================================
    // EXECUTION GATE CHECK
    // =========================================================================
    struct GateResult {
        bool allowed;
        TradeOutcome outcome;
        BlockReason reason;
    };
    
    [[nodiscard]] GateResult checkExecution(
        const char* symbol,
        SymbolType symbol_type,
        bool is_exit_order = false
    ) const {
        GateResult result;
        result.allowed = false;
        result.outcome = TradeOutcome::SUPPRESSED;
        result.reason = BlockReason::NONE;
        
        // Get current intent
        IntentState intent = getIntent(symbol);
        
        // Check execution permission
        if (!canExecute(intent, symbol_type, is_exit_order)) {
            result.reason = BlockReason::INTENT_NOT_LIVE;
            result.outcome = TradeOutcome::BLOCKED;  // BLOCKED = gate working correctly
            return result;
        }
        
        // Probe check for CFDs
        if (!is_exit_order && 
            (symbol_type == SymbolType::CFD_FOREX ||
             symbol_type == SymbolType::CFD_METAL ||
             symbol_type == SymbolType::CFD_INDEX)) {
            if (!config_.probes_allowed_cfd && intent != IntentState::LIVE) {
                result.reason = BlockReason::PROBE_DISABLED;
                result.outcome = TradeOutcome::SUPPRESSED;
                return result;
            }
        }
        
        // Execution allowed
        result.allowed = true;
        result.outcome = TradeOutcome::EXECUTED;
        return result;
    }
    
    // =========================================================================
    // STANDBY MODE
    // =========================================================================
    void enterStandby(uint64_t now_ns) {
        standby_ = true;
        forceAllState(IntentState::NO_TRADE, now_ns);
        printf("[INTENT-GATE] ════════════════════════════════════════\n");
        printf("[INTENT-GATE] ENGINE OFF — NO EDGE EXPECTED\n");
        printf("[INTENT-GATE] ════════════════════════════════════════\n");
    }
    
    void exitStandby() {
        standby_ = false;
        printf("[INTENT-GATE] Standby exited - ready to evaluate\n");
    }
    
    [[nodiscard]] bool isStandby() const { return standby_; }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    void printStatus() const {
        printf("[INTENT-GATE] Status (standby=%s):\n", standby_ ? "YES" : "NO");
        for (const auto& [symbol, intent] : intents_) {
            printf("  %s: %s (edge=%.2f conv=%.2f changes=%u)\n",
                   symbol.c_str(),
                   intent_state_str(intent.state.load()),
                   intent.current_edge.load(),
                   intent.current_conviction.load(),
                   intent.state_change_count.load());
        }
    }

private:
    Config config_;
    std::unordered_map<std::string, SymbolIntent> intents_;
    bool standby_ = false;
    
    void logTransition(const char* symbol, IntentState from, IntentState to,
                       double edge, double conviction, uint64_t ts_ns) {
        (void)ts_ns;  // Suppress unused parameter warning
        printf("[INTENT] %s: %s → %s (edge=%.2f conv=%.2f)\n",
               symbol, intent_state_str(from), intent_state_str(to),
               edge, conviction);
    }
};

// =============================================================================
// GLOBAL INTENT GATE ACCESS
// =============================================================================
inline IntentGate& getIntentGate() {
    static IntentGate instance;
    return instance;
}

} // namespace Chimera
