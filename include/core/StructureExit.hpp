#pragma once
// =============================================================================
// CHIMERA STRUCTURE EXIT - v4.7.0
// =============================================================================
// Exits are STRUCTURAL, not time-based or fixed TP/SL.
// NEVER: Fixed TP, Fixed SL, Time exits
// =============================================================================

#include <cstdint>
#include <cstring>
#include <cstdio>

namespace Chimera {

struct MicrostructureState {
    double edge = 0.0;
    double imbalance = 0.0;
    double persistence = 0.0;
    double range_expansion = 0.0;
    int direction = 0;
    bool imbalance_flipped = false;
    
    void reset() {
        edge = 0.0; imbalance = 0.0; persistence = 0.0;
        range_expansion = 0.0; direction = 0; imbalance_flipped = false;
    }
};

struct LatencyMetrics {
    double rtt_us = 0.0;
    double rtt_p95_us = 0.0;
    double jitter_us = 0.0;
    double reject_rate = 0.0;
    double baseline_rtt_us = 0.0;
    
    bool isDegraded() const {
        if (rtt_p95_us > 0 && rtt_us > 1.8 * rtt_p95_us) return true;
        if (jitter_us > 250.0) return true;
        if (reject_rate > 0.02) return true;
        return false;
    }
    
    bool isElevated() const {
        return (rtt_p95_us > 0 && rtt_us > 1.3 * rtt_p95_us);
    }
};

struct PositionState {
    char symbol[16] = {0};
    int direction = 0;
    double entry_edge = 0.0;
    double entry_price = 0.0;
    double current_price = 0.0;
    uint64_t entry_time_ns = 0;
    bool in_profit = false;
    
    double unrealizedPnlBps() const {
        if (entry_price <= 0.0) return 0.0;
        double pnl = (current_price - entry_price) / entry_price * 10000.0;
        return direction * pnl;
    }
};

enum class ExitReason : uint8_t {
    NONE = 0, EDGE_DECAY, IMBALANCE_FLIP, RANGE_EXPANSION,
    LATENCY_DEGRADED, SHOCK_DETECTED, VENUE_UNHEALTHY,
    DAILY_LOSS_HIT, MANUAL_EXIT, INTENT_CHANGED, TIME_CAP
};

inline const char* exitReasonToString(ExitReason r) {
    switch (r) {
        case ExitReason::NONE:             return "NONE";
        case ExitReason::EDGE_DECAY:       return "EDGE_DECAY";
        case ExitReason::IMBALANCE_FLIP:   return "IMBALANCE_FLIP";
        case ExitReason::RANGE_EXPANSION:  return "RANGE_EXPANSION";
        case ExitReason::LATENCY_DEGRADED: return "LATENCY_DEGRADED";
        case ExitReason::SHOCK_DETECTED:   return "SHOCK_DETECTED";
        case ExitReason::VENUE_UNHEALTHY:  return "VENUE_UNHEALTHY";
        case ExitReason::DAILY_LOSS_HIT:   return "DAILY_LOSS_HIT";
        case ExitReason::MANUAL_EXIT:      return "MANUAL_EXIT";
        case ExitReason::INTENT_CHANGED:   return "INTENT_CHANGED";
        case ExitReason::TIME_CAP:         return "TIME_CAP";
        default:                           return "UNKNOWN";
    }
}

// =============================================================================
// v4.8.0 FIX #3: Structure Resolving State for Time Cap Extension
// =============================================================================
enum class StructureResolvingState : uint8_t {
    NOT_RESOLVING = 0,
    RESOLVING_SLOW = 1,     // Structure is resolving but slowly
    RESOLVING_ACTIVE = 2    // Structure is actively resolving
};

// Time cap configuration with extension for resolving structures
struct TimeCap {
    static constexpr double BASE_TIME_CAP_SEC = 3.5;
    static constexpr double STRUCTURE_EXTENSION_SEC = 3.0;  // +3s when structure is resolving
    
    static double getTimeCap(StructureResolvingState resolving_state) {
        switch (resolving_state) {
            case StructureResolvingState::RESOLVING_SLOW:
            case StructureResolvingState::RESOLVING_ACTIVE:
                return BASE_TIME_CAP_SEC + STRUCTURE_EXTENSION_SEC;
            default:
                return BASE_TIME_CAP_SEC;
        }
    }
};

class StructureExit {
public:
    static bool shouldExit(
        const PositionState& pos,
        const MicrostructureState& micro,
        const LatencyMetrics& latency,
        bool shock_detected,
        bool venue_healthy,
        ExitReason* out_reason = nullptr
    ) {
        double edge_threshold = 0.35;
        if (strcmp(pos.symbol, "XAUUSD") == 0 || strcmp(pos.symbol, "NAS100") == 0) {
            edge_threshold = 0.50;
        }
        
        if (pos.entry_edge > 0 && micro.edge < pos.entry_edge * edge_threshold) {
            if (out_reason) *out_reason = ExitReason::EDGE_DECAY;
            return true;
        }
        
        if (micro.imbalance_flipped && micro.persistence < 0.40) {
            if (out_reason) *out_reason = ExitReason::IMBALANCE_FLIP;
            return true;
        }
        
        if (micro.range_expansion > 2.2 && micro.direction != pos.direction) {
            if (out_reason) *out_reason = ExitReason::RANGE_EXPANSION;
            return true;
        }
        
        if (latency.isDegraded()) {
            if (out_reason) *out_reason = ExitReason::LATENCY_DEGRADED;
            return true;
        }
        
        if ((strcmp(pos.symbol, "XAUUSD") == 0 || strcmp(pos.symbol, "NAS100") == 0) &&
            latency.isElevated()) {
            if (out_reason) *out_reason = ExitReason::LATENCY_DEGRADED;
            return true;
        }
        
        if (shock_detected) {
            if (!pos.in_profit) {
                if (out_reason) *out_reason = ExitReason::SHOCK_DETECTED;
                return true;
            }
            if (micro.edge < pos.entry_edge * 0.60) {
                if (out_reason) *out_reason = ExitReason::SHOCK_DETECTED;
                return true;
            }
        }
        
        if (!venue_healthy) {
            if (out_reason) *out_reason = ExitReason::VENUE_UNHEALTHY;
            return true;
        }
        
        if (out_reason) *out_reason = ExitReason::NONE;
        return false;
    }
    
    static bool shouldExitQuick(double current_edge, double entry_edge,
                                 bool imbalance_flipped, double persistence) {
        if (entry_edge > 0 && current_edge < entry_edge * 0.35) return true;
        if (imbalance_flipped && persistence < 0.40) return true;
        return false;
    }
    
    // =========================================================================
    // v4.8.0 FIX #3: Structure-specific exit with time cap extension
    // =========================================================================
    // If structure is resolving, extend time cap by +3s to avoid early scratches
    static bool shouldExitStructure(
        const PositionState& pos,
        const MicrostructureState& micro,
        const LatencyMetrics& latency,
        bool shock_detected,
        bool venue_healthy,
        StructureResolvingState resolving_state,
        uint64_t now_ns,
        ExitReason* out_reason = nullptr
    ) {
        // First check all non-time-based exits
        if (shouldExit(pos, micro, latency, shock_detected, venue_healthy, out_reason)) {
            return true;
        }
        
        // Time-based exit with structure extension
        double held_sec = static_cast<double>(now_ns - pos.entry_time_ns) / 1e9;
        double time_cap = TimeCap::getTimeCap(resolving_state);
        
        // Only apply time cap if not in profit
        if (held_sec > time_cap && !pos.in_profit) {
            if (out_reason) *out_reason = ExitReason::TIME_CAP;
            return true;
        }
        
        return false;
    }
};

} // namespace Chimera
