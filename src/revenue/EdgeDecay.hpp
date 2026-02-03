#pragma once

namespace chimera {

// ---------------------------------------------------------------------------
// EdgeDecay — Latency-adjusted edge filter
//
// Filters trades that would be profitable with zero latency but negative
// after accounting for execution delay. Essential for VPS trading where
// 2-10ms latency can erode thin edges.
//
// Example:
//   Raw edge: 2bps
//   Latency: 5ms
//   Decay coefficient: 0.5 bps/ms
//   Effective edge: 2 - (5 * 0.5) = -0.5 bps → REJECT
//
// This prevents submitting orders that are statistically unprofitable
// by the time they reach the exchange.
//
// CRITICAL: Supports both millisecond and nanosecond latency inputs
// to prevent unit confusion bugs.
// ---------------------------------------------------------------------------

class EdgeDecay {
public:
    // decay_coef = basis points of edge lost per millisecond of latency
    // Typical range: 0.3-1.0 bps/ms depending on symbol volatility
    explicit EdgeDecay(double decay_coef = 0.5);
    
    // Calculate effective edge after latency decay (MILLISECONDS)
    // edge_bps = raw edge in basis points
    // latency_ms = round-trip latency to exchange in MILLISECONDS
    // Returns: adjusted edge in basis points
    double adjust_edge_ms(double edge_bps, double latency_ms) const;
    
    // Check if trade is viable after latency adjustment (MILLISECONDS)
    // Returns true only if adjusted edge > 0
    bool is_viable_ms(double edge_bps, double latency_ms) const;
    
    // Calculate effective edge after latency decay (NANOSECONDS)
    // edge_bps = raw edge in basis points
    // latency_ns = round-trip latency to exchange in NANOSECONDS
    // Returns: adjusted edge in basis points
    double adjust_edge_ns(double edge_bps, double latency_ns) const;
    
    // Check if trade is viable after latency adjustment (NANOSECONDS)
    // Returns true only if adjusted edge > 0
    bool is_viable_ns(double edge_bps, double latency_ns) const;
    
    // Get current decay coefficient
    double get_decay_coefficient() const { return decay_coef_; }
    
    // Update decay coefficient (can be tuned based on market conditions)
    void set_decay_coefficient(double coef) { decay_coef_ = coef; }

private:
    double decay_coef_;  // bps lost per ms of latency
};

} // namespace chimera
