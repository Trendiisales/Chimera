#pragma once

#include <atomic>
#include <thread>
#include "../core/PerformanceTracker.hpp"
#include "../risk/RiskGovernorV3.hpp"
#include "../execution/LatencyEngineV2.hpp"

namespace chimera {
namespace core {

struct AdaptiveParams {
    std::atomic<double> hft_signal_threshold{0.6};
    std::atomic<double> structure_conf_threshold{0.7};
    std::atomic<double> spread_limit{0.5};
    std::atomic<double> vol_limit{5.0};
    std::atomic<double> capital_bias{1.0};
    
    static constexpr double MIN_HFT_THRESHOLD = 0.3;
    static constexpr double MAX_HFT_THRESHOLD = 0.9;
    static constexpr double MIN_STRUCT_THRESHOLD = 0.4;
    static constexpr double MAX_STRUCT_THRESHOLD = 0.95;
    static constexpr double MIN_SPREAD = 0.2;
    static constexpr double MAX_SPREAD = 1.2;
    static constexpr double MIN_VOL = 2.0;
    static constexpr double MAX_VOL = 15.0;
};

class AdaptiveOptimizerV2 {
public:
    AdaptiveOptimizerV2(AdaptiveParams& params,
                        PerformanceTracker& perf,
                        risk::RiskGovernorV3& risk,
                        execution::LatencyEngineV2& latency);

    void start();
    void stop();
    
    const AdaptiveParams& get_params() const { return m_params; }

private:
    void optimization_loop();
    double compute_sharpe(EngineType engine);
    void apply_quality_throttle();
    
    static constexpr double HYSTERESIS_BAND = 0.15;  // FIX: Prevents oscillation

    AdaptiveParams& m_params;
    PerformanceTracker& m_perf;
    risk::RiskGovernorV3& m_risk;
    execution::LatencyEngineV2& m_latency;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace core
} // namespace chimera
