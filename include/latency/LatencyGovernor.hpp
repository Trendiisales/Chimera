#pragma once

#include <array>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════
// LATENCY GOVERNOR - Jitter-Aware Execution Gate
// ═══════════════════════════════════════════════════════════════════════════
//
// Derives execution regimes from rolling RTT percentiles.
// Enforces instrument-specific latency policies.
//
// REGIMES (derived from empirical VPS measurements):
//   FAST:     p95 ≤ 6ms  AND p99 ≤ 12ms AND current ≤ 8ms
//   NORMAL:   p95 ≤ 10ms AND p99 ≤ 18ms AND current ≤ 14ms
//   DEGRADED: otherwise
//
// POLICIES:
//   XAU: FAST-only (no trading in NORMAL/DEGRADED)
//   XAG: Disabled only in DEGRADED
//
// ═══════════════════════════════════════════════════════════════════════════

enum class LatencyRegime {
    FAST = 0,      // Historical normal - trade freely
    NORMAL = 1,    // Marginal but usable - restrict XAU
    DEGRADED = 2   // Physics says stop - exits only
};

class LatencyGovernor {
public:
    static constexpr int WINDOW = 2048;  // Rolling window size

    LatencyGovernor();

    // Record new RTT measurement
    void record_rtt_ms(double rtt_ms);

    // Get current regime
    LatencyRegime regime() const;

    // Entry gate (pre-execution check)
    bool allow_entry(const std::string& symbol) const;

    // TIME exit gate (pre-exit check)
    bool allow_time_exit(const std::string& symbol) const;

    // Statistics
    double p50() const;
    double p90() const;
    double p95() const;
    double p99() const;
    double current() const;

private:
    std::array<double, WINDOW> samples_;
    int count_;
    int head_;
    double last_;

    double percentile(double p) const;
};
