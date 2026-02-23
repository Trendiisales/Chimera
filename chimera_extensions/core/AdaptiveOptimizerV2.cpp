#include "AdaptiveOptimizerV2.hpp"
#include <chrono>
#include <algorithm>

namespace chimera {
namespace core {

AdaptiveOptimizerV2::AdaptiveOptimizerV2(AdaptiveParams& params,
                                         PerformanceTracker& perf,
                                         risk::RiskGovernorV3& risk,
                                         execution::LatencyEngineV2& latency)
    : m_params(params), m_perf(perf), m_risk(risk), m_latency(latency) {}

void AdaptiveOptimizerV2::start() {
    m_running.store(true);
    m_thread = std::thread(&AdaptiveOptimizerV2::optimization_loop, this);
}

void AdaptiveOptimizerV2::stop() {
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.join();
}

double AdaptiveOptimizerV2::compute_sharpe(EngineType engine) {
    return m_perf.compute_score(engine);
}

void AdaptiveOptimizerV2::apply_quality_throttle() {
    double quality = m_latency.get_quality_ema();
    
    if (quality < 0.6) {
        double current_hft = m_params.hft_signal_threshold.load();
        double current_struct = m_params.structure_conf_threshold.load();
        
        m_params.hft_signal_threshold.store(
            std::min(AdaptiveParams::MAX_HFT_THRESHOLD, current_hft + 0.05));
        m_params.structure_conf_threshold.store(
            std::min(AdaptiveParams::MAX_STRUCT_THRESHOLD, current_struct + 0.05));
    }
}

void AdaptiveOptimizerV2::optimization_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        // FIX #4: Freeze adaptive updates during lockdown
        if (m_risk.is_lockdown_active()) {
            continue;  // Skip all adaptive changes during shock events
        }
        
        double hft_sharpe = compute_sharpe(EngineType::HFT);
        double struct_sharpe = compute_sharpe(EngineType::STRUCTURE);
        
        // FIX #2: Hysteresis band prevents oscillation
        
        // HFT tuning with hysteresis
        if (hft_sharpe > 0.7 + HYSTERESIS_BAND) {
            double current = m_params.hft_signal_threshold.load();
            m_params.hft_signal_threshold.store(
                std::max(AdaptiveParams::MIN_HFT_THRESHOLD, current - 0.05));
        }
        else if (hft_sharpe < 0.4 - HYSTERESIS_BAND) {
            double current = m_params.hft_signal_threshold.load();
            m_params.hft_signal_threshold.store(
                std::min(AdaptiveParams::MAX_HFT_THRESHOLD, current + 0.05));
        }
        
        // Structure tuning with hysteresis
        if (struct_sharpe > 0.7 + HYSTERESIS_BAND) {
            double current = m_params.structure_conf_threshold.load();
            m_params.structure_conf_threshold.store(
                std::max(AdaptiveParams::MIN_STRUCT_THRESHOLD, current - 0.05));
        }
        else if (struct_sharpe < 0.4 - HYSTERESIS_BAND) {
            double current = m_params.structure_conf_threshold.load();
            m_params.structure_conf_threshold.store(
                std::min(AdaptiveParams::MAX_STRUCT_THRESHOLD, current + 0.05));
        }
        
        // Risk tightening with bounds
        if (hft_sharpe < 0.3 && struct_sharpe < 0.3) {
            double current_spread = m_params.spread_limit.load();
            double current_vol = m_params.vol_limit.load();
            
            current_spread *= 0.95;
            current_vol *= 0.9;
            
            m_params.spread_limit.store(
                std::clamp(current_spread, AdaptiveParams::MIN_SPREAD, AdaptiveParams::MAX_SPREAD));
            m_params.vol_limit.store(
                std::clamp(current_vol, AdaptiveParams::MIN_VOL, AdaptiveParams::MAX_VOL));
        }
        
        // Capital bias with hysteresis
        if (hft_sharpe > struct_sharpe + HYSTERESIS_BAND) {
            m_params.capital_bias.store(1.2);
        } else if (struct_sharpe > hft_sharpe + HYSTERESIS_BAND) {
            m_params.capital_bias.store(0.8);
        }
        // else: no change (prevents ping-pong)
        
        apply_quality_throttle();
    }
}

} // namespace core
} // namespace chimera
