#include "revenue/EdgeDecay.hpp"

namespace chimera {

EdgeDecay::EdgeDecay(double decay_coef)
    : decay_coef_(decay_coef) {}

double EdgeDecay::adjust_edge_ms(double edge_bps, double latency_ms) const {
    // Simple linear decay model: edge decays proportionally to latency
    // More sophisticated models could use:
    // - Exponential decay for very high latency
    // - Volatility-adjusted decay
    // - Symbol-specific decay rates
    return edge_bps - (latency_ms * decay_coef_);
}

bool EdgeDecay::is_viable_ms(double edge_bps, double latency_ms) const {
    return adjust_edge_ms(edge_bps, latency_ms) > 0.0;
}

double EdgeDecay::adjust_edge_ns(double edge_bps, double latency_ns) const {
    // Convert nanoseconds to milliseconds
    double latency_ms = latency_ns / 1'000'000.0;
    return adjust_edge_ms(edge_bps, latency_ms);
}

bool EdgeDecay::is_viable_ns(double edge_bps, double latency_ns) const {
    return adjust_edge_ns(edge_bps, latency_ns) > 0.0;
}

} // namespace chimera
