// =============================================================================
// TradeDecisionState.hpp - v4.8.0 - "WHY NOT TRADING" PANEL
// =============================================================================
// PURPOSE: Real-time visibility into WHY each symbol is or isn't trading
//
// This is the CRITICAL missing piece for operations.
// Without this, you're tuning blind.
//
// PUBLISHED VIA: WebSocket to dashboard (per-symbol state)
//
// USAGE:
//   TradeDecisionState state;
//   state.update(symbol, session, profile, market_state, blocker);
//   g_gui.broadcastDecisionState(state);
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <array>
#include <chrono>

#include "ScalpProfile.hpp"

namespace Chimera {

// =============================================================================
// VETO REASON (comprehensive)
// =============================================================================
enum class VetoReason : uint8_t {
    NONE = 0,           // Ready to trade
    
    // Session/Time vetoes
    WRONG_SESSION,
    OFF_HOURS,
    SESSION_UNSTABLE,   // v4.8.0: Session not yet resolved
    
    // Microstructure vetoes
    REGIME_TOXIC,
    REGIME_TRANSITION,
    EDGE_TOO_LOW,
    PERSISTENCE_LOW,
    IMBALANCE_WEAK,
    SPREAD_TOO_WIDE,
    RANGE_EXPANDED,
    
    // Latency/Venue vetoes
    LATENCY_DEGRADED,
    FIX_NOT_CONNECTED,
    VENUE_UNHEALTHY,
    
    // Risk vetoes
    SHOCK_DETECTED,
    DAILY_LOSS_HIT,
    MAX_TRADES_HIT,
    CONSECUTIVE_LOSSES,
    RISK_BLOCKED,
    
    // Execution vetoes
    INTENT_NOT_LIVE,
    SYMBOL_BLOCKED,
    OWNERSHIP_DENIED,
    COOLDOWN_ACTIVE,
    
    // ML vetoes
    ML_VETO,
    ML_FAIL_CLOSED,
    
    // Structure vetoes (for INDEX_STRUCTURE profile)
    NO_COMPRESSION,
    NO_ABSORPTION,
    NO_RESOLUTION,
    
    // v4.8.0: Trigger state (gates passed but waiting for trigger)
    WAITING_FOR_TRIGGER,
    
    // Catch-all
    UNKNOWN
};

inline const char* vetoReasonToString(VetoReason r) {
    switch (r) {
        case VetoReason::NONE:                return "READY";
        case VetoReason::WRONG_SESSION:       return "WRONG_SESSION";
        case VetoReason::OFF_HOURS:           return "OFF_HOURS";
        case VetoReason::SESSION_UNSTABLE:    return "SESSION_UNSTABLE";
        case VetoReason::REGIME_TOXIC:        return "REGIME_TOXIC";
        case VetoReason::REGIME_TRANSITION:   return "REGIME_TRANSITION";
        case VetoReason::EDGE_TOO_LOW:        return "EDGE_TOO_LOW";
        case VetoReason::PERSISTENCE_LOW:     return "PERSISTENCE_LOW";
        case VetoReason::IMBALANCE_WEAK:      return "IMBALANCE_WEAK";
        case VetoReason::SPREAD_TOO_WIDE:     return "SPREAD_TOO_WIDE";
        case VetoReason::RANGE_EXPANDED:      return "RANGE_EXPANDED";
        case VetoReason::LATENCY_DEGRADED:    return "LATENCY_DEGRADED";
        case VetoReason::FIX_NOT_CONNECTED:   return "FIX_NOT_CONNECTED";
        case VetoReason::VENUE_UNHEALTHY:     return "VENUE_UNHEALTHY";
        case VetoReason::SHOCK_DETECTED:      return "SHOCK_DETECTED";
        case VetoReason::DAILY_LOSS_HIT:      return "DAILY_LOSS_HIT";
        case VetoReason::MAX_TRADES_HIT:      return "MAX_TRADES_HIT";
        case VetoReason::CONSECUTIVE_LOSSES:  return "CONSECUTIVE_LOSSES";
        case VetoReason::RISK_BLOCKED:        return "RISK_BLOCKED";
        case VetoReason::INTENT_NOT_LIVE:     return "INTENT_NOT_LIVE";
        case VetoReason::SYMBOL_BLOCKED:      return "SYMBOL_BLOCKED";
        case VetoReason::OWNERSHIP_DENIED:    return "OWNERSHIP_DENIED";
        case VetoReason::COOLDOWN_ACTIVE:     return "COOLDOWN_ACTIVE";
        case VetoReason::ML_VETO:             return "ML_VETO";
        case VetoReason::ML_FAIL_CLOSED:      return "ML_FAIL_CLOSED";
        case VetoReason::NO_COMPRESSION:      return "NO_COMPRESSION";
        case VetoReason::NO_ABSORPTION:       return "NO_ABSORPTION";
        case VetoReason::NO_RESOLUTION:       return "NO_RESOLUTION";
        case VetoReason::WAITING_FOR_TRIGGER: return "WAITING_FOR_TRIGGER";
        case VetoReason::UNKNOWN:             return "UNKNOWN";
        default:                              return "UNKNOWN";
    }
}

// Convert ScalpBlocker to VetoReason
inline VetoReason scalpBlockerToVeto(ScalpBlocker b) {
    switch (b) {
        case ScalpBlocker::NONE:               return VetoReason::NONE;
        case ScalpBlocker::WRONG_SESSION:      return VetoReason::WRONG_SESSION;
        case ScalpBlocker::REGIME_TOXIC:       return VetoReason::REGIME_TOXIC;
        case ScalpBlocker::EDGE_TOO_LOW:       return VetoReason::EDGE_TOO_LOW;
        case ScalpBlocker::PERSISTENCE_LOW:    return VetoReason::PERSISTENCE_LOW;
        case ScalpBlocker::IMBALANCE_WEAK:     return VetoReason::IMBALANCE_WEAK;
        case ScalpBlocker::SPREAD_TOO_WIDE:    return VetoReason::SPREAD_TOO_WIDE;
        case ScalpBlocker::RANGE_EXPANSION:    return VetoReason::RANGE_EXPANDED;
        case ScalpBlocker::LATENCY_NOT_NORMAL: return VetoReason::LATENCY_DEGRADED;
        case ScalpBlocker::SHOCK_ACTIVE:       return VetoReason::SHOCK_DETECTED;
        case ScalpBlocker::DAILY_LOSS_HIT:     return VetoReason::DAILY_LOSS_HIT;
        case ScalpBlocker::MAX_TRADES_HIT:     return VetoReason::MAX_TRADES_HIT;
        case ScalpBlocker::CONSECUTIVE_LOSSES: return VetoReason::CONSECUTIVE_LOSSES;
        case ScalpBlocker::SYMBOL_NOT_ALLOWED: return VetoReason::SYMBOL_BLOCKED;
        case ScalpBlocker::PROFILE_DISABLED:   return VetoReason::SYMBOL_BLOCKED;
        default:                               return VetoReason::UNKNOWN;
    }
}

// =============================================================================
// STRUCTURE STATE (for INDEX_STRUCTURE profile)
// =============================================================================
enum class StructureState : uint8_t {
    EXPANDED = 0,       // Range expanded, not tradable
    COMPRESSED = 1,     // Watching for breakout
    RESOLVING = 2,      // Trigger armed
    FAILED = 3          // Cooldown after failed breakout
};

inline const char* structureStateToString(StructureState s) {
    switch (s) {
        case StructureState::EXPANDED:   return "EXPANDED";
        case StructureState::COMPRESSED: return "COMPRESSED";
        case StructureState::RESOLVING:  return "RESOLVING";
        case StructureState::FAILED:     return "FAILED";
        default:                         return "UNKNOWN";
    }
}

// =============================================================================
// TRADE DECISION STATE - Per-Symbol Real-Time Status
// =============================================================================
struct TradeDecisionState {
    // Identity
    char symbol[16] = {0};
    
    // Profile & Session
    ActivityProfile profile = ActivityProfile::DISABLED;
    Session session = Session::OFF_HOURS;
    
    // Decision
    bool allowed = false;
    VetoReason veto_reason = VetoReason::NONE;
    
    // Microstructure metrics (what was measured)
    double edge = 0.0;
    double edge_threshold = 0.0;
    double persistence = 0.0;
    double persistence_threshold = 0.0;
    double spread_bps = 0.0;
    double spread_threshold = 0.0;
    double imbalance = 0.0;
    double imbalance_threshold = 0.0;
    double range_expansion = 0.0;
    double range_threshold = 0.0;
    
    // Regime
    Regime regime = Regime::STABLE;
    bool regime_stable = true;
    
    // Latency
    LatencyState latency = LatencyState::NORMAL;
    double latency_ms = 0.0;
    
    // Shock
    bool shock_active = false;
    double shock_cooldown_sec = 0.0;
    
    // Structure (for INDEX_STRUCTURE)
    StructureState structure = StructureState::EXPANDED;
    double absorption_score = 0.0;
    double range_percentile = 0.0;
    
    // Connection
    bool fix_connected = false;
    bool intent_live = false;
    
    // v4.8.0 FIX #2: Trigger state visibility
    bool waiting_for_trigger = false;   // Gates passed but waiting for trigger
    bool session_stable = true;         // Session has resolved
    
    // Timing
    uint64_t last_update_ns = 0;
    
    // =========================================================================
    // UPDATE FROM SCALP EVALUATION
    // =========================================================================
    void updateFromScalp(
        const char* sym,
        Session sess,
        ActivityProfile prof,
        const ScalpMarketState& market,
        ScalpBlocker blocker,
        double edge_req,
        double pers_req,
        double spread_max,
        double imb_req,
        double range_cap
    ) {
        strncpy(symbol, sym, sizeof(symbol) - 1);
        session = sess;
        profile = prof;
        
        // Decision
        allowed = (blocker == ScalpBlocker::NONE);
        veto_reason = scalpBlockerToVeto(blocker);
        
        // Metrics
        edge = market.edge;
        edge_threshold = edge_req;
        persistence = market.persistence;
        persistence_threshold = pers_req;
        spread_bps = market.spread;
        spread_threshold = spread_max;
        imbalance = market.imbalance;
        imbalance_threshold = imb_req;
        range_expansion = market.range_expansion;
        range_threshold = range_cap;
        
        // Regime
        regime = market.regime;
        regime_stable = (market.regime == Regime::STABLE || market.regime == Regime::TRANSITION);
        
        // Latency
        latency = market.latency;
        
        // Shock
        shock_active = market.shock_active;
        
        // Timestamp
        last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // =========================================================================
    // UPDATE FROM INDEX STRUCTURE EVALUATION
    // =========================================================================
    void updateFromIndexStructure(
        const char* sym,
        Session sess,
        StructureState struct_state,
        double absorption,
        double range_pct,
        double edge_val,
        double pers_val,
        Regime reg,
        LatencyState lat,
        bool shock,
        VetoReason veto
    ) {
        strncpy(symbol, sym, sizeof(symbol) - 1);
        session = sess;
        profile = ActivityProfile::CORE;  // INDEX_STRUCTURE uses CORE
        
        // Structure-specific
        structure = struct_state;
        absorption_score = absorption;
        range_percentile = range_pct;
        
        // Decision
        allowed = (veto == VetoReason::NONE);
        veto_reason = veto;
        
        // Metrics
        edge = edge_val;
        edge_threshold = 0.75;  // INDEX_STRUCTURE requires 0.75
        persistence = pers_val;
        persistence_threshold = 0.60;
        
        // Regime
        regime = reg;
        regime_stable = (reg == Regime::STABLE || reg == Regime::TRANSITION);
        
        // Latency
        latency = lat;
        
        // Shock
        shock_active = shock;
        
        // Timestamp
        last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // =========================================================================
    // SET CONNECTION STATE
    // =========================================================================
    void setConnectionState(bool fix_conn, bool intent) {
        fix_connected = fix_conn;
        intent_live = intent;
        
        if (!intent) {
            allowed = false;
            veto_reason = VetoReason::INTENT_NOT_LIVE;
        } else if (!fix_conn && profile != ActivityProfile::CORE) {
            // CFD scalp needs FIX
            allowed = false;
            veto_reason = VetoReason::FIX_NOT_CONNECTED;
        }
    }
    
    // =========================================================================
    // JSON SERIALIZATION (for WebSocket broadcast)
    // =========================================================================
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"symbol\":\"%s\","
            "\"profile\":\"%s\","
            "\"session\":\"%s\","
            "\"session_stable\":%s,"
            "\"allowed\":%s,"
            "\"veto_reason\":\"%s\","
            "\"waiting_for_trigger\":%s,"
            "\"edge\":%.3f,"
            "\"edge_threshold\":%.3f,"
            "\"persistence\":%.3f,"
            "\"persistence_threshold\":%.3f,"
            "\"spread\":%.2f,"
            "\"spread_threshold\":%.2f,"
            "\"imbalance\":%.3f,"
            "\"imbalance_threshold\":%.3f,"
            "\"range_expansion\":%.2f,"
            "\"range_threshold\":%.2f,"
            "\"regime\":\"%s\","
            "\"regime_stable\":%s,"
            "\"latency_state\":\"%s\","
            "\"latency_ms\":%.2f,"
            "\"shock_active\":%s,"
            "\"structure_state\":\"%s\","
            "\"absorption\":%.3f,"
            "\"range_percentile\":%.1f,"
            "\"fix_connected\":%s,"
            "\"intent_live\":%s"
            "}",
            symbol,
            profileToString(profile),
            sessionToString(session),
            session_stable ? "true" : "false",
            allowed ? "true" : "false",
            vetoReasonToString(veto_reason),
            waiting_for_trigger ? "true" : "false",
            edge, edge_threshold,
            persistence, persistence_threshold,
            spread_bps, spread_threshold,
            imbalance, imbalance_threshold,
            range_expansion, range_threshold,
            regime == Regime::STABLE ? "STABLE" :
            regime == Regime::TRANSITION ? "TRANSITION" :
            regime == Regime::TRENDING ? "TRENDING" : "TOXIC",
            regime_stable ? "true" : "false",
            latency == LatencyState::NORMAL ? "NORMAL" :
            latency == LatencyState::ELEVATED ? "ELEVATED" : "DEGRADED",
            latency_ms,
            shock_active ? "true" : "false",
            structureStateToString(structure),
            absorption_score, range_percentile,
            fix_connected ? "true" : "false",
            intent_live ? "true" : "false"
        );
    }
    
    // =========================================================================
    // PRINT STATUS (console diagnostics)
    // =========================================================================
    void printStatus() const {
        const char* status_icon = allowed ? "✔" : "✖";
        const char* edge_icon = edge >= edge_threshold ? "✔" : "✖";
        const char* pers_icon = persistence >= persistence_threshold ? "✔" : "✖";
        const char* lat_icon = latency == LatencyState::NORMAL ? "✔" : "✖";
        const char* shock_icon = !shock_active ? "✔" : "✖";
        const char* session_icon = session_stable ? "✔" : "⏳";
        const char* trigger_icon = waiting_for_trigger ? "⏳" : "✔";
        
        printf("\n╔════════════════════════════════════════════════════════════╗\n");
        printf("║  WHY-NOT-TRADING: %-12s                             ║\n", symbol);
        printf("╠════════════════════════════════════════════════════════════╣\n");
        printf("║  Profile:  %-12s  Session: %-12s %s        ║\n", 
               profileToString(profile), sessionToString(session), session_icon);
        printf("║  Status:   %s %-20s                         ║\n", 
               status_icon, vetoReasonToString(veto_reason));
        
        // v4.8.0: Show trigger state when gates passed
        if (waiting_for_trigger) {
            printf("║  Trigger:  %s WAITING_FOR_TRIGGER                          ║\n", trigger_icon);
        }
        
        printf("╠════════════════════════════════════════════════════════════╣\n");
        printf("║  Edge:        %.2f / %.2f %s                              ║\n", 
               edge, edge_threshold, edge_icon);
        printf("║  Persistence: %.2f / %.2f %s                              ║\n", 
               persistence, persistence_threshold, pers_icon);
        
        if (profile == ActivityProfile::SCALP_LDN) {
            const char* spread_icon = spread_bps <= spread_threshold ? "✔" : "✖";
            const char* range_icon = range_expansion < range_threshold ? "✔" : "✖";
            printf("║  Spread:      %.2f / %.2f %s                            ║\n",
                   spread_bps, spread_threshold, spread_icon);
            printf("║  Range:       %.2f / %.2f %s                            ║\n",
                   range_expansion, range_threshold, range_icon);
        }
        
        printf("║  Latency:     %s %s                                       ║\n",
               latency == LatencyState::NORMAL ? "NORMAL" : 
               (latency == LatencyState::ELEVATED ? "ELEVATED" : "DEGRADED"), lat_icon);
        printf("║  Shock:       %s %s                                       ║\n",
               shock_active ? "ACTIVE" : "CLEAR", shock_icon);
        printf("║  Structure:   %-12s                                   ║\n",
               structureStateToString(structure));
        printf("║  FIX:         %s  Intent: %s                              ║\n",
               fix_connected ? "CONNECTED" : "DISCONNECTED",
               intent_live ? "LIVE" : "NOT_LIVE");
        printf("╚════════════════════════════════════════════════════════════╝\n\n");
    }
};

// =============================================================================
// DECISION STATE MANAGER - Tracks all symbols
// =============================================================================
class DecisionStateManager {
public:
    static constexpr size_t MAX_SYMBOLS = 16;
    
    static DecisionStateManager& instance() {
        static DecisionStateManager inst;
        return inst;
    }
    
    TradeDecisionState* getState(const char* symbol) {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(states_[i].symbol, symbol) == 0) {
                return &states_[i];
            }
        }
        
        // Add new symbol if space
        if (count_ < MAX_SYMBOLS) {
            strncpy(states_[count_].symbol, symbol, 15);
            return &states_[count_++];
        }
        
        return nullptr;  // Full
    }
    
    void printAllStatus() const {
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("  TRADE DECISION STATUS (v4.8.0)\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        
        for (size_t i = 0; i < count_; ++i) {
            states_[i].printStatus();
        }
    }
    
    // Get all states as JSON array
    void toJSONArray(char* buf, size_t buf_size) const {
        size_t offset = 0;
        offset += snprintf(buf + offset, buf_size - offset, "[");
        
        for (size_t i = 0; i < count_; ++i) {
            char state_json[1024];
            states_[i].toJSON(state_json, sizeof(state_json));
            offset += snprintf(buf + offset, buf_size - offset, "%s%s",
                              i > 0 ? "," : "", state_json);
        }
        
        snprintf(buf + offset, buf_size - offset, "]");
    }
    
    size_t symbolCount() const { return count_; }
    
private:
    DecisionStateManager() : count_(0) {}
    
    std::array<TradeDecisionState, MAX_SYMBOLS> states_;
    size_t count_;
};

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================
inline DecisionStateManager& getDecisionStateManager() { 
    return DecisionStateManager::instance(); 
}

inline TradeDecisionState* getDecisionState(const char* symbol) {
    return DecisionStateManager::instance().getState(symbol);
}

} // namespace Chimera
