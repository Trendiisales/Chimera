#pragma once
#include <unordered_map>
#include <string>

namespace chimera {

// ---------------------------------------------------------------------------
// LatencyEVGate: Block trades where edge has decayed due to latency
// 
// Problem: By the time order arrives, alpha has decayed
// Solution: Block if (latency * decay_rate) > raw_edge
// 
// Usage:
//   LatencyEVGate ev_gate;
//   ev_gate.set_latency_ms("ETHUSDT", 2.5);
//   
//   // Before submission
//   if (!ev_gate.allow("ETHUSDT", raw_edge_bps))
//       return;  // Edge too small for latency
// 
// Effect: Stops submitting orders that will be negative by arrival
// Expected gain: +1-3 bps (avoids bad fills)
// ---------------------------------------------------------------------------

class LatencyEVGate {
public:
    LatencyEVGate(double cost_per_ms = 0.5)
        : cost_per_ms_(cost_per_ms) {}

    // Set latency for a symbol (in milliseconds)
    void set_latency_ms(const std::string& sym, double ms) {
        latency_[sym] = ms;
    }

    // Check if edge is sufficient given latency
    bool allow(const std::string& sym, double raw_edge_bps) const {
        auto it = latency_.find(sym);
        if (it == latency_.end())
            return true;  // No latency data = allow

        double cost = it->second * cost_per_ms_;
        return raw_edge_bps > cost;
    }

    // Get latency cost for a symbol
    double cost_bps(const std::string& sym) const {
        auto it = latency_.find(sym);
        if (it == latency_.end())
            return 0.0;
        return it->second * cost_per_ms_;
    }

private:
    std::unordered_map<std::string, double> latency_;
    double cost_per_ms_;  // Edge decay per ms of latency (0.5 bps/ms)
};

} // namespace chimera
