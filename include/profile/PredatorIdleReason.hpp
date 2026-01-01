// =============================================================================
// PredatorIdleReason.hpp - v4.8.0 - PREDATOR IDLE REASON TRACKING
// =============================================================================
// PURPOSE: Track and expose exactly why Predator is idle
//
// This prevents:
//   - Second-guessing
//   - Over-tuning
//   - Operator panic
//
// If Predator is idle, you MUST know exactly why.
// Only ONE reason is shown — the highest priority blocker.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>

namespace Chimera {

// =============================================================================
// IDLE REASONS (in priority order, highest first)
// =============================================================================
enum class PredatorIdleReason : uint8_t {
    NONE = 0,                   // Not idle, trading enabled
    GO_NO_GO_BLOCK = 1,         // GoNoGoGate says NO_GO
    LATENCY_UNSTABLE = 2,       // Latency is degraded
    SESSION_DISABLED = 3,       // Current session is OFF (e.g., Asia)
    REGIME_TOXIC = 4,           // Market regime is toxic
    STRUCTURE_NOT_READY = 5,    // Structure not resolving/confirmed
    SYMBOL_DISABLED = 6,        // Symbol not in Predator config
    EDGE_NOT_PRESENT = 7,       // No valid entry signal
    COOLDOWN_ACTIVE = 8,        // Post-trade cooldown
    TRADE_LIMIT_REACHED = 9,    // Max trades for session hit
    CONSECUTIVE_LOSSES = 10,    // 2+ consecutive losses
    POSITION_OPEN = 11          // Already have an open position
};

inline const char* predatorIdleReasonToString(PredatorIdleReason r) {
    switch (r) {
        case PredatorIdleReason::NONE:                return "NONE";
        case PredatorIdleReason::GO_NO_GO_BLOCK:      return "GO_NO_GO_BLOCK";
        case PredatorIdleReason::LATENCY_UNSTABLE:    return "LATENCY_UNSTABLE";
        case PredatorIdleReason::SESSION_DISABLED:    return "SESSION_DISABLED";
        case PredatorIdleReason::REGIME_TOXIC:        return "REGIME_TOXIC";
        case PredatorIdleReason::STRUCTURE_NOT_READY: return "STRUCTURE_NOT_READY";
        case PredatorIdleReason::SYMBOL_DISABLED:     return "SYMBOL_DISABLED";
        case PredatorIdleReason::EDGE_NOT_PRESENT:    return "EDGE_NOT_PRESENT";
        case PredatorIdleReason::COOLDOWN_ACTIVE:     return "COOLDOWN_ACTIVE";
        case PredatorIdleReason::TRADE_LIMIT_REACHED: return "TRADE_LIMIT_REACHED";
        case PredatorIdleReason::CONSECUTIVE_LOSSES:  return "CONSECUTIVE_LOSSES";
        case PredatorIdleReason::POSITION_OPEN:       return "POSITION_OPEN";
        default:                                      return "UNKNOWN";
    }
}

inline const char* predatorIdleReasonIcon(PredatorIdleReason r) {
    switch (r) {
        case PredatorIdleReason::NONE:                return "✅";
        case PredatorIdleReason::GO_NO_GO_BLOCK:      return "🔴";
        case PredatorIdleReason::LATENCY_UNSTABLE:    return "🔴";
        case PredatorIdleReason::SESSION_DISABLED:    return "⏸️";
        case PredatorIdleReason::REGIME_TOXIC:        return "☢️";
        case PredatorIdleReason::STRUCTURE_NOT_READY: return "⏳";
        case PredatorIdleReason::SYMBOL_DISABLED:     return "🚫";
        case PredatorIdleReason::EDGE_NOT_PRESENT:    return "⏳";
        case PredatorIdleReason::COOLDOWN_ACTIVE:     return "❄️";
        case PredatorIdleReason::TRADE_LIMIT_REACHED: return "🛑";
        case PredatorIdleReason::CONSECUTIVE_LOSSES:  return "⚠️";
        case PredatorIdleReason::POSITION_OPEN:       return "📍";
        default:                                      return "❓";
    }
}

// =============================================================================
// IDLE STATUS STRUCTURE
// =============================================================================
struct PredatorIdleStatus {
    PredatorIdleReason reason = PredatorIdleReason::NONE;
    const char* symbol = "";
    uint64_t timestamp_ns = 0;
    
    bool isIdle() const { return reason != PredatorIdleReason::NONE; }
    bool canTrade() const { return reason == PredatorIdleReason::NONE; }
    
    void print() const {
        printf("[PREDATOR] %s IDLE: %s %s\n",
               symbol,
               predatorIdleReasonIcon(reason),
               predatorIdleReasonToString(reason));
    }
    
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"type\":\"predator_idle\","
            "\"symbol\":\"%s\","
            "\"reason\":\"%s\","
            "\"can_trade\":%s"
            "}",
            symbol,
            predatorIdleReasonToString(reason),
            canTrade() ? "true" : "false"
        );
    }
};

} // namespace Chimera
