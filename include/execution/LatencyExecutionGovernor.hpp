#pragma once
#include <deque>
#include <cstdint>
#include <string>

enum class LatencyRegime {
    FAST,
    NORMAL,
    DEGRADED,
    HALT
};

struct LatencySnapshot {
    double p50_ms;
    double p90_ms;
    double p95_ms;
    double p99_ms;
    LatencyRegime regime;
};

class LatencyExecutionGovernor {
public:
    LatencyExecutionGovernor();
    void on_rtt(double rtt_ms, uint64_t now_ms);
    void on_loop_heartbeat(uint64_t now_ms);
    
    LatencySnapshot snapshot() const;
    LatencyRegime regime() const { return regime_; }

private:
    void evaluate_regime();
    
    std::deque<double> rtt_samples_;
    static constexpr size_t MAX_SAMPLES = 1024;
    
    LatencyRegime regime_ = LatencyRegime::FAST;
    uint64_t last_heartbeat_ms_ = 0;
    uint64_t last_regime_change_ms_ = 0;
    bool in_stall_recovery_ = false;
    
    // XAU-tuned thresholds (from measured data)
    static constexpr double FAST_THRESHOLD_MS = 6.0;
    static constexpr double NORMAL_THRESHOLD_MS = 10.0;
    static constexpr double DEGRADED_THRESHOLD_MS = 15.0;
    static constexpr double HALT_THRESHOLD_MS = 25.0;
    static constexpr uint64_t STALL_THRESHOLD_MS = 3000;
    static constexpr uint64_t REGIME_HYSTERESIS_MS = 3000;
};
