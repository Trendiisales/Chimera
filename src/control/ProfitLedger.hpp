#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <vector>
#include <mutex>
#include <cstdint>
#include <algorithm>

namespace chimera {

class Context;

// ---------------------------------------------------------------------------
// ProfitLedger — institutional-grade per-engine profitability tracking.
//
// Every fill produces a complete cost/edge record. From these we compute:
//   EV_ema_bps     — 100-fill EMA of net_bps (structural profitability)
//   FillRate       — fills / total_attempts
//   CancelRate     — cancels / total_attempts
//   LatencyP95     — 95th percentile of order latency
//
// These four numbers gate everything:
//   - Admission: predicted_edge must beat real_cost * SAFETY_MULT
//   - Kill: EV_ema < -3.0bps sustained 3 min → engine killed
//   - Auto-tune: EV drives min_edge and size_multiplier adjustments
//   - Arm: EV > +5bps + FillRate > 20% + LatencyP95 < 2500µs = arm-eligible
//
// Threading: on_submit/on_fill/on_cancel called from CORE1.
//   on_price called from CORE1 poll (vol tracker).
//   to_json called from telemetry thread — acquires mtx_.
// ---------------------------------------------------------------------------
class ProfitLedger {
public:
    explicit ProfitLedger(Context& ctx);

    // ---------------------------------------------------------------------------
    // Per-engine initialization. Call from main() before trading.
    // min_edge_bps: starting edge floor for this engine.
    // size_mult: starting size multiplier (1.0 = strategy's native size).
    // soft_ttl_fill_prob: per-engine queue competitiveness threshold.
    // ---------------------------------------------------------------------------
    void set_engine_defaults(const std::string& engine_id,
                             double min_edge_bps,
                             double size_mult = 1.0,
                             double soft_ttl_fill_prob = 0.35);

    // ---------------------------------------------------------------------------
    // Submit event — increment submit counter per engine.
    // ---------------------------------------------------------------------------
    void on_submit(const std::string& engine_id);

    // ---------------------------------------------------------------------------
    // Fill event — the core data point. Records full cost/edge breakdown.
    // Updates EV, fill rate, latency tracking. Checks kill condition.
    // Triggers auto-tune if interval elapsed.
    // ---------------------------------------------------------------------------
    void on_fill(const std::string& engine_id,
                 const std::string& symbol,
                 bool is_buy,
                 double fill_price,
                 double fill_qty,
                 uint64_t submit_ns,
                 double latency_us,
                 double predicted_edge_bps,
                 double realized_edge_bps,
                 double fee_bps,
                 double slippage_bps,
                 double pnl_usd,
                 double net_bps);

    // ---------------------------------------------------------------------------
    // Cancel event — increment cancel counter per engine.
    // ---------------------------------------------------------------------------
    void on_cancel(const std::string& engine_id);

    // ---------------------------------------------------------------------------
    // Volatility feed — call each poll tick with current mid price per symbol.
    // Drives the latency_bps component of real cost.
    // ---------------------------------------------------------------------------
    void on_price(const std::string& symbol, double mid, uint64_t ts_ns);

    // ---------------------------------------------------------------------------
    // Admission threshold — the dynamic edge floor for this engine.
    // Returns: max(real_cost_bps * SAFETY_MULT, engine_min_edge_bps)
    //
    // real_cost_bps = fee_bps + latency_bps + queue_bps where:
    //   fee_bps      = 10.0 (Binance spot)
    //   latency_bps  = (latency_us / 1000.0) * vol_bps_per_ms
    //   queue_bps    = (1 - fill_prob) * spread_bps * 0.5
    // ---------------------------------------------------------------------------
    double admission_threshold(const std::string& engine_id,
                               const std::string& symbol,
                               double latency_us,
                               double fill_prob,
                               bool is_buy);

    // ---------------------------------------------------------------------------
    // Parameter queries — called by ExecutionRouter each tick.
    // ---------------------------------------------------------------------------
    double get_min_edge(const std::string& engine_id) const;
    double get_size_multiplier(const std::string& engine_id) const;
    double get_soft_ttl_fill_prob(const std::string& engine_id) const;

    // ---------------------------------------------------------------------------
    // JSON dump for /profit telemetry endpoint.
    // ---------------------------------------------------------------------------
    std::string to_json() const;

private:
    // -----------------------------------------------------------------------
    // Per-engine state — all mutable fields protected by mtx_.
    // -----------------------------------------------------------------------
    struct EngineMetrics {
        // Tunable parameters (auto-tuner writes these)
        double min_edge_bps{15.0};
        double size_multiplier{1.0};
        double soft_ttl_fill_prob{0.35};

        // Counters
        uint64_t submits{0};
        uint64_t fills{0};
        uint64_t cancels{0};

        // Rolling metrics
        double ev_ema_bps{0.0};         // 100-fill EMA of net_bps
        double net_pnl_usd{0.0};        // cumulative net PnL

        // Kill state
        bool alive{true};
        uint64_t ev_negative_since_ns{0}; // when EV first went below threshold (0 = not negative)

        // Latency samples for P95 (unsorted, sorted on query)
        std::deque<double> latency_samples;
        static constexpr size_t LATENCY_WINDOW = 200;
    };

    // -----------------------------------------------------------------------
    // Per-symbol volatility state
    // -----------------------------------------------------------------------
    struct VolState {
        double prev_mid{0.0};
        uint64_t prev_ts_ns{0};
        double vol_bps_per_ms{0.0};  // EMA of |price_change_bps| / dt_ms
    };

    // -----------------------------------------------------------------------
    // Kill check — called after each fill.
    // EV_ema < -3.0bps sustained for 3 min → block_engine via PnLGovernor.
    // -----------------------------------------------------------------------
    void check_kill(const std::string& engine_id, EngineMetrics& m, uint64_t now);

    // -----------------------------------------------------------------------
    // Auto-tune — called when 5-min interval elapses.
    // Adjusts min_edge, size_multiplier, soft_ttl_fill_prob per engine.
    // -----------------------------------------------------------------------
    void auto_tune();

    // -----------------------------------------------------------------------
    // Latency P95 from engine's sample window. Caller holds mtx_.
    // -----------------------------------------------------------------------
    static double latency_p95(const EngineMetrics& m);

    // -----------------------------------------------------------------------
    // Spread cache per symbol — updated in admission_threshold from caller's
    // TopOfBook. Avoids re-reading the book inside the mutex.
    // -----------------------------------------------------------------------
    struct SpreadCache {
        double spread_bps{1.0};
        uint64_t ts_ns{0};
    };

    Context& ctx_;

    std::unordered_map<std::string, EngineMetrics> engines_;
    std::unordered_map<std::string, VolState>      vol_;
    std::unordered_map<std::string, SpreadCache>   spread_cache_;

    uint64_t last_autotune_ns{0};

    // Constants
    static constexpr uint64_t AUTOTUNE_INTERVAL_NS = 300'000'000'000ULL; // 5 min
    static constexpr double   SAFETY_MULT          = 1.5;
    static constexpr double   FEE_BPS              = 10.0;
    static constexpr double   EV_KILL_THRESHOLD    = -3.0;               // bps
    static constexpr uint64_t EV_KILL_SUSTAIN_NS   = 180'000'000'000ULL; // 3 min
    static constexpr double   EV_EMA_ALPHA         = 0.01;               // 1/100 fills

    mutable std::mutex mtx_;
};

} // namespace chimera
