// ═══════════════════════════════════════════════════════════════════════════════
// include/symbol/ColoPlaybook.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: PER-SYMBOL COLO PLAYBOOKS
//
// PURPOSE: Each instrument trades differently in colo vs WAN.
// These playbooks define optimal parameters for each physics class.
//
// PARAMETERS:
// - Maker timeout (how long to wait)
// - Repost interval (when to cancel/repost)
// - Minimum edge (what's tradeable)
// - Queue behavior (how to estimate fills)
//
// ACTIVATED: Only in COLO physics. Otherwise defaults used.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include "runtime/ExecutionPhysics.hpp"

namespace Chimera {
namespace Symbol {

// ─────────────────────────────────────────────────────────────────────────────
// Colo Playbook Parameters
// ─────────────────────────────────────────────────────────────────────────────
struct ColoPlaybook {
    // Timing (ms)
    double maker_timeout_ms = 5.0;
    double repost_interval_ms = 3.0;
    double cancel_deadline_ms = 2.0;
    
    // Edge thresholds (bps)
    double min_edge_bps = 1.5;
    double target_edge_bps = 3.0;
    
    // Queue behavior
    double queue_position_factor = 1.0;   // Multiplier for queue depth
    double fill_probability_boost = 0.0;  // Bonus to fill probability
    
    // Size adjustments
    double size_multiplier = 1.0;
    double max_position_multiplier = 1.0;
    
    // Execution mode
    bool prefer_maker = true;
    bool allow_aggressive_repost = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Default Playbooks by Symbol
// ─────────────────────────────────────────────────────────────────────────────
inline ColoPlaybook playbookForSymbol(const char* symbol) {
    ColoPlaybook p;
    
    // XAUUSD - Gold
    if (strcmp(symbol, "XAUUSD") == 0) {
        p.maker_timeout_ms = 3.0;
        p.repost_interval_ms = 2.0;
        p.min_edge_bps = 0.8;
        p.target_edge_bps = 2.0;
        p.prefer_maker = true;
        p.allow_aggressive_repost = true;
        return p;
    }
    
    // XAGUSD - Silver
    if (strcmp(symbol, "XAGUSD") == 0) {
        p.maker_timeout_ms = 3.5;
        p.repost_interval_ms = 2.5;
        p.min_edge_bps = 1.0;
        p.target_edge_bps = 2.5;
        p.prefer_maker = true;
        return p;
    }
    
    // NAS100 - Nasdaq
    if (strcmp(symbol, "NAS100") == 0) {
        p.maker_timeout_ms = 2.5;
        p.repost_interval_ms = 1.8;
        p.min_edge_bps = 0.9;
        p.target_edge_bps = 2.0;
        p.queue_position_factor = 0.8;  // Faster queue
        p.prefer_maker = true;
        return p;
    }
    
    // US30 - Dow Jones
    if (strcmp(symbol, "US30") == 0) {
        p.maker_timeout_ms = 2.5;
        p.repost_interval_ms = 1.8;
        p.min_edge_bps = 0.9;
        p.target_edge_bps = 2.0;
        p.prefer_maker = true;
        return p;
    }
    
    // BTCUSDT - Bitcoin
    if (strcmp(symbol, "BTCUSDT") == 0) {
        p.maker_timeout_ms = 1.2;
        p.repost_interval_ms = 1.0;
        p.min_edge_bps = 1.4;
        p.target_edge_bps = 3.0;
        p.prefer_maker = false;  // Taker preferred even in colo
        p.allow_aggressive_repost = false;
        return p;
    }
    
    // ETHUSDT - Ethereum
    if (strcmp(symbol, "ETHUSDT") == 0) {
        p.maker_timeout_ms = 1.5;
        p.repost_interval_ms = 1.2;
        p.min_edge_bps = 1.5;
        p.target_edge_bps = 3.5;
        p.prefer_maker = false;
        return p;
    }
    
    // SOLUSDT - Solana
    if (strcmp(symbol, "SOLUSDT") == 0) {
        p.maker_timeout_ms = 1.8;
        p.repost_interval_ms = 1.5;
        p.min_edge_bps = 2.0;
        p.target_edge_bps = 4.0;
        p.prefer_maker = false;
        p.size_multiplier = 0.8;  // More volatile, reduce size
        return p;
    }
    
    // Default
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Adjust Playbook for Physics
// ─────────────────────────────────────────────────────────────────────────────
inline ColoPlaybook adjustForPhysics(
    const ColoPlaybook& base,
    Runtime::ExecPhysics physics
) {
    ColoPlaybook p = base;
    
    switch (physics) {
        case Runtime::ExecPhysics::COLO:
            // Full playbook as defined
            break;
            
        case Runtime::ExecPhysics::NEAR_COLO:
            // Relax timing, increase edge requirements
            p.maker_timeout_ms *= 1.5;
            p.repost_interval_ms *= 1.5;
            p.min_edge_bps *= 1.3;
            p.allow_aggressive_repost = false;
            break;
            
        case Runtime::ExecPhysics::WAN:
        default:
            // Disable colo tactics entirely
            p.maker_timeout_ms = 220.0;  // Effectively disabled
            p.repost_interval_ms = 1000.0;
            p.min_edge_bps *= 2.0;
            p.prefer_maker = false;
            p.allow_aggressive_repost = false;
            p.size_multiplier = 0.5;
            break;
    }
    
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get Effective Playbook for Symbol and Physics
// ─────────────────────────────────────────────────────────────────────────────
inline ColoPlaybook getPlaybook(
    const char* symbol,
    Runtime::ExecPhysics physics
) {
    ColoPlaybook base = playbookForSymbol(symbol);
    return adjustForPhysics(base, physics);
}

// ─────────────────────────────────────────────────────────────────────────────
// Playbook Summary String
// ─────────────────────────────────────────────────────────────────────────────
inline void playbookStr(const ColoPlaybook& p, char* buf, size_t len) {
    snprintf(buf, len,
             "TIMEOUT=%.1fms REPOST=%.1fms EDGE=%.1fbps MAKER=%c",
             p.maker_timeout_ms,
             p.repost_interval_ms,
             p.min_edge_bps,
             p.prefer_maker ? 'Y' : 'N');
}

} // namespace Symbol
} // namespace Chimera
