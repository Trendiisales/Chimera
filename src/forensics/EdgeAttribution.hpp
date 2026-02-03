#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace chimera {

class Context;

// ---------------------------------------------------------------------------
// Edge Attribution — per-engine fill quality forensics.
//
// On submit: predicted edge (bps) recorded.
// On fill:   realized PnL (bps) compared. Leak = predicted - realized.
//
// Per-engine EWMAs tracked:
//   edge_leak         — how much predicted edge vanishes at fill
//   latency_sens      — leak * latency (is latency the leak source?)
//   win_rate          — cumulative
//
// Kill logic: if edge_leak OR latency_sens exceeds threshold → PnL Governor
// blocks that engine. Cancel Federation fires to flatten any open orders from
// that engine. This does NOT kill other engines.
//
// This is DIAGNOSTIC + KILL. It tells you WHY an engine is losing, and kills
// it before the loss compounds.
//
// Threading: on_submit / on_fill called from CORE1 (ExecutionRouter).
// stats() called from telemetry thread — engines_ is append-only so read
// of fully-constructed EngineStats is safe without lock (writes only happen
// on CORE1 via on_fill).
// ---------------------------------------------------------------------------
class EdgeAttribution {
public:
    explicit EdgeAttribution(Context& ctx);

    void on_submit(const std::string& order_id,
                   const std::string& engine_id,
                   double predicted_edge_bps,
                   double queue_pos);

    void on_fill(const std::string& order_id,
                 double realized_pnl_bps,
                 double latency_us);

    // ---------------------------------------------------------------------------
    // Cancel cleanup — removes pending_ entry for an order that was canceled
    // (TTL timeout, cancel-replace, or exchange reject/expire).
    // Without this, pending_ grows unbounded over the lifetime of the process.
    // ---------------------------------------------------------------------------
    void on_cancel(const std::string& order_id);

    struct EngineStats {
        double   ewma_edge_leak{0.0};
        double   ewma_latency_sens{0.0};
        double   win_rate{0.0};
        uint64_t trades{0};
    };

    const EngineStats& stats(const std::string& engine_id) const;

    void set_alpha(double a)                     { alpha_ = a; }
    void set_max_edge_leak_bps(double t)         { max_edge_leak_bps_ = t; }
    void set_max_latency_sensitivity(double t)   { max_latency_sens_ = t; }

private:
    struct Pending {
        std::string engine_id;
        double      predicted_edge_bps;
        double      queue_pos;
    };

    Context& ctx_;

    std::unordered_map<std::string, Pending>      pending_;  // order_id → submit context
    std::unordered_map<std::string, EngineStats>  engines_;  // engine_id → rolling stats

    double alpha_{0.05};
    double max_edge_leak_bps_{1.5};
    double max_latency_sens_{0.002};
};

} // namespace chimera
