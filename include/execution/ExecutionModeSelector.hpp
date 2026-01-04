// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ExecutionModeSelector.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: INSTITUTIONAL EXECUTION MODE SELECTION
//
// PURPOSE: Dynamically choose maker vs taker based on:
// 1. Execution physics (COLO/NEAR_COLO/WAN)
// 2. Measured fill/reject rates
// 3. Current market conditions
//
// CRITICAL: Maker is only allowed when physics supports it.
// No fantasy maker usage on WAN or crypto.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <algorithm>
#include "runtime/ExecutionCapabilities.hpp"

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecModeSelection : uint8_t {
    MAKER_ONLY     = 0,  // Post-only limit orders
    TAKER_ONLY     = 1,  // Market orders only
    MAKER_FIRST    = 2,  // Try maker, fallback to taker
    ADAPTIVE       = 3   // System chooses per-trade
};

inline const char* execModeSelectionStr(ExecModeSelection m) {
    switch (m) {
        case ExecModeSelection::MAKER_ONLY:  return "MAKER_ONLY";
        case ExecModeSelection::TAKER_ONLY:  return "TAKER_ONLY";
        case ExecModeSelection::MAKER_FIRST: return "MAKER_FIRST";
        case ExecModeSelection::ADAPTIVE:    return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode Selection Result
// ─────────────────────────────────────────────────────────────────────────────
struct ExecModeResult {
    ExecModeSelection mode = ExecModeSelection::TAKER_ONLY;
    const char* reason = "DEFAULT";
    double confidence = 0.5;  // 0-1, how confident in this choice
};

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct ExecModeConfig {
    // Thresholds for switching to TAKER
    double reject_rate_taker_threshold = 0.15;     // >15% rejects → taker
    double fill_rate_taker_threshold = 0.40;       // <40% fills → taker  
    double ack_p95_ms_taker_threshold = 3.0;       // >3ms latency → taker
    double edge_spread_ratio_taker = 1.5;          // Edge > 1.5x spread → taker
    
    // Symbol-specific overrides
    bool force_taker_crypto = true;                // BTC/ETH/SOL always taker
    bool force_maker_indices = false;              // NAS100/US30 maker preferred
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol-Specific Latency Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolLatencyEnvelope {
    char symbol[16] = {0};
    double max_ack_p95_ms = 3.0;       // Max acceptable latency
    double target_ack_p95_ms = 1.0;    // Ideal latency
    double maker_timeout_ms = 5.0;     // Max time to wait for maker fill
};

inline SymbolLatencyEnvelope getLatencyEnvelope(const char* symbol) {
    SymbolLatencyEnvelope env;
    strncpy(env.symbol, symbol, 15);
    
    // Crypto: Very tight latency requirements
    if (symbol[0] == 'B' || symbol[0] == 'E' || symbol[0] == 'S') {
        env.max_ack_p95_ms = 2.0;
        env.target_ack_p95_ms = 0.5;
        env.maker_timeout_ms = 1.5;
        return env;
    }
    
    // Gold/Silver: Medium latency
    if (symbol[0] == 'X') {
        env.max_ack_p95_ms = 5.0;
        env.target_ack_p95_ms = 2.0;
        env.maker_timeout_ms = 4.0;
        return env;
    }
    
    // Indices: More tolerant
    if (symbol[0] == 'N' || symbol[0] == 'U') {
        env.max_ack_p95_ms = 8.0;
        env.target_ack_p95_ms = 3.0;
        env.maker_timeout_ms = 5.0;
        return env;
    }
    
    return env;
};

// ─────────────────────────────────────────────────────────────────────────────
// Choose Execution Mode - PHYSICS-AWARE VERSION
// ─────────────────────────────────────────────────────────────────────────────
inline ExecModeResult chooseExecMode(
    const Runtime::ExecCapabilities& cap,
    double recent_fill_rate,      // 0-1, fill rate over last N orders
    double reject_rate,           // 0-1, reject rate over last N orders
    double ack_p95_ms,            // p95 ACK latency in milliseconds
    double spread_bps,            // Current spread in basis points
    double edge_bps,              // Expected edge in basis points
    bool high_volatility,         // Is market in volatile state?
    const ExecModeConfig& cfg = ExecModeConfig{}
) {
    ExecModeResult result;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 0: Physics doesn't allow maker → TAKER
    // ─────────────────────────────────────────────────────────────────────────
    if (!cap.allow_maker) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "PHYSICS_NO_MAKER";
        result.confidence = 0.95;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 1: High volatility → TAKER (urgency beats spread capture)
    // ─────────────────────────────────────────────────────────────────────────
    if (high_volatility) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "HIGH_VOL_URGENCY";
        result.confidence = 0.90;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 2: High reject rate → TAKER (venue instability)
    // ─────────────────────────────────────────────────────────────────────────
    if (reject_rate > cfg.reject_rate_taker_threshold) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "REJECT_RATE_HIGH";
        result.confidence = 0.85;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 3: Low fill rate → TAKER (queue not viable)
    // ─────────────────────────────────────────────────────────────────────────
    if (recent_fill_rate < cfg.fill_rate_taker_threshold) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "FILL_RATE_LOW";
        result.confidence = 0.80;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 4: High latency → TAKER (can't compete for queue)
    // ─────────────────────────────────────────────────────────────────────────
    if (ack_p95_ms > cfg.ack_p95_ms_taker_threshold) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "LATENCY_TOO_HIGH";
        result.confidence = 0.85;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 5: Edge >> Spread → TAKER (speed matters more than spread capture)
    // ─────────────────────────────────────────────────────────────────────────
    if (spread_bps > 0 && edge_bps > spread_bps * cfg.edge_spread_ratio_taker) {
        result.mode = ExecModeSelection::TAKER_ONLY;
        result.reason = "EDGE_EXCEEDS_SPREAD";
        result.confidence = 0.75;
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Default: MAKER_FIRST (try maker, fallback to taker on timeout)
    // ─────────────────────────────────────────────────────────────────────────
    result.mode = ExecModeSelection::MAKER_FIRST;
    result.reason = "PHYSICS_ALLOWS_MAKER";
    result.confidence = cap.confidence * 0.8;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Choose Execution Mode - SYMBOL-AWARE VERSION
// ─────────────────────────────────────────────────────────────────────────────
inline ExecModeResult chooseExecModeForSymbol(
    const char* symbol,
    const Runtime::ExecCapabilities& cap,
    double recent_fill_rate,
    double reject_rate,
    double ack_p95_ms,
    double spread_bps,
    double edge_bps,
    bool high_volatility,
    const ExecModeConfig& cfg = ExecModeConfig{}
) {
    // Get symbol-specific latency envelope
    SymbolLatencyEnvelope env = getLatencyEnvelope(symbol);
    
    // Use symbol-specific threshold instead of global
    ExecModeConfig symbol_cfg = cfg;
    symbol_cfg.ack_p95_ms_taker_threshold = env.max_ack_p95_ms;
    
    return chooseExecMode(cap, recent_fill_rate, reject_rate, ack_p95_ms,
                          spread_bps, edge_bps, high_volatility, symbol_cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Legacy function for backward compatibility
// ─────────────────────────────────────────────────────────────────────────────
inline ExecModeResult chooseExecMode(
    double recent_fill_rate,
    double reject_rate,
    double ack_p95_ms,
    double spread_bps,
    double edge_bps,
    bool high_volatility,
    bool is_crypto,
    const ExecModeConfig& cfg = ExecModeConfig{}
) {
    // Build capabilities based on simple heuristics
    Runtime::ExecCapabilities cap;
    cap.physics = Runtime::ExecPhysics::WAN;
    cap.allow_maker = !is_crypto && ack_p95_ms < 5.0;
    cap.confidence = 0.5;
    
    return chooseExecMode(cap, recent_fill_rate, reject_rate, ack_p95_ms,
                          spread_bps, edge_bps, high_volatility, cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol-specific mode defaults
// ─────────────────────────────────────────────────────────────────────────────
inline ExecModeSelection getDefaultModeForSymbol(const char* symbol) {
    // Crypto: TAKER only (no queue position without colo)
    if (symbol[0] == 'B' || symbol[0] == 'E' || symbol[0] == 'S') {
        // BTCUSDT, ETHUSDT, SOLUSDT
        return ExecModeSelection::TAKER_ONLY;
    }
    
    // Gold/Silver: Maker first (CFD has reasonable fills)
    if (symbol[0] == 'X') {
        // XAUUSD, XAGUSD
        return ExecModeSelection::MAKER_FIRST;
    }
    
    // Indices: Maker primary (good queue dynamics)
    if (symbol[0] == 'N' || symbol[0] == 'U') {
        // NAS100, US30
        return ExecModeSelection::MAKER_FIRST;
    }
    
    return ExecModeSelection::TAKER_ONLY;
}

} // namespace Execution
} // namespace Chimera
