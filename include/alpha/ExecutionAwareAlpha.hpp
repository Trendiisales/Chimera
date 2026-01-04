// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/ExecutionAwareAlpha.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: EXECUTION-AWARE ALPHA FILTERING
//
// PURPOSE: Price execution cost directly into alpha evaluation.
// Weak edges never reach execution - they're killed at signal generation.
//
// PRINCIPLE:
// Real alpha = Raw signal edge - Execution costs
//
// EXECUTION COSTS:
// - Spread crossing (taker)
// - Latency slippage
// - Cancel latency risk
// - Fill uncertainty
//
// If real alpha < threshold, intent = NO_TRADE before execution layer even sees it.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Cost Components (all in bps)
// ─────────────────────────────────────────────────────────────────────────────
struct ExecutionCosts {
    double spread_bps = 0.0;          // Half-spread or full spread (taker)
    double latency_slippage_bps = 0.0; // Expected slippage from latency
    double cancel_risk_bps = 0.0;      // Cost of failed cancels
    double fill_uncertainty_bps = 0.0; // Cost of partial/no fills
    double commission_bps = 0.0;       // Commission
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute Total Execution Cost
// ─────────────────────────────────────────────────────────────────────────────
inline double totalExecutionCost(const ExecutionCosts& c) {
    return c.spread_bps + 
           c.latency_slippage_bps + 
           c.cancel_risk_bps + 
           c.fill_uncertainty_bps + 
           c.commission_bps;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Execution Cost from Latency
// ─────────────────────────────────────────────────────────────────────────────
inline double executionCostBps(
    double ack_p95_ms,
    double cancel_p95_ms,
    double spread_bps,
    bool is_maker,
    double slippage_per_ms = 0.05,      // bps per ms of latency
    double cancel_risk_per_ms = 0.02    // bps per ms of cancel latency
) {
    double cost = 0.0;
    
    // Spread cost
    if (is_maker) {
        cost += spread_bps * 0.5;  // Half spread for maker
    } else {
        cost += spread_bps;        // Full spread for taker (cross)
    }
    
    // Latency slippage
    cost += ack_p95_ms * slippage_per_ms;
    
    // Cancel risk (for maker orders)
    if (is_maker) {
        cost += cancel_p95_ms * cancel_risk_per_ms;
    }
    
    return cost;
}

// ─────────────────────────────────────────────────────────────────────────────
// Does Alpha Survive Execution Costs?
// ─────────────────────────────────────────────────────────────────────────────
inline bool alphaSurvives(
    double raw_edge_bps,
    double exec_cost_bps,
    double safety_margin_bps = 0.3
) {
    return raw_edge_bps > (exec_cost_bps + safety_margin_bps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Quality Assessment
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaQuality {
    double raw_edge_bps = 0.0;
    double exec_cost_bps = 0.0;
    double net_edge_bps = 0.0;
    double quality_ratio = 0.0;     // raw / (raw + cost)
    bool viable = false;
    const char* grade = "F";
};

inline AlphaQuality assessAlpha(
    double raw_edge_bps,
    double ack_p95_ms,
    double cancel_p95_ms,
    double spread_bps,
    bool is_maker,
    double commission_bps = 0.0,
    double safety_margin_bps = 0.3
) {
    AlphaQuality q;
    q.raw_edge_bps = raw_edge_bps;
    
    // Compute execution cost
    q.exec_cost_bps = executionCostBps(ack_p95_ms, cancel_p95_ms, spread_bps, is_maker);
    q.exec_cost_bps += commission_bps;
    
    // Net edge
    q.net_edge_bps = raw_edge_bps - q.exec_cost_bps;
    
    // Quality ratio
    double denominator = q.raw_edge_bps + q.exec_cost_bps;
    q.quality_ratio = denominator > 0 ? q.raw_edge_bps / denominator : 0.0;
    
    // Viability
    q.viable = q.net_edge_bps > safety_margin_bps;
    
    // Grade
    if (q.net_edge_bps > 3.0) q.grade = "A+";
    else if (q.net_edge_bps > 2.0) q.grade = "A";
    else if (q.net_edge_bps > 1.0) q.grade = "B";
    else if (q.net_edge_bps > 0.5) q.grade = "C";
    else if (q.net_edge_bps > 0.0) q.grade = "D";
    else q.grade = "F";
    
    return q;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Decay Model (edge decays over time)
// ─────────────────────────────────────────────────────────────────────────────
inline double alphaDecay(
    double initial_edge_bps,
    double elapsed_ms,
    double half_life_ms = 50.0    // Edge halves every 50ms
) {
    double decay_factor = std::pow(0.5, elapsed_ms / half_life_ms);
    return initial_edge_bps * decay_factor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimum Edge for Physics Class
// ─────────────────────────────────────────────────────────────────────────────
inline double minEdgeForPhysics(
    double base_min_edge_bps,
    double ack_p95_ms
) {
    // COLO: Can trade smaller edges
    if (ack_p95_ms < 1.5) {
        return std::max(0.8, base_min_edge_bps * 0.6);
    }
    
    // NEAR_COLO: Moderate reduction
    if (ack_p95_ms < 8.0) {
        return std::max(1.5, base_min_edge_bps * 0.8);
    }
    
    // WAN: No reduction, possibly increase
    return base_min_edge_bps * 1.1;
}

} // namespace Alpha
} // namespace Chimera
