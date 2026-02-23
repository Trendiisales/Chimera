#include "AdaptiveOptimizer.hpp"
#include <chrono>
#include <algorithm>

namespace chimera {
namespace core {

AdaptiveOptimizer::AdaptiveOptimizer(AdaptiveParams& params,
                                     PerformanceTracker& perf,
                                     risk::RiskGovernorV2& risk,
                                     execution::LatencyEngineV2& latency)
    : m_params(params)
    , m_perf(perf)
    , m_risk(risk)
    , m_latency(latency) {}

void AdaptiveOptimizer::start() {
    m_running.store(true);
    m_thread = std::thread(&AdaptiveOptimizer::optimization_loop, this);
}

void AdaptiveOptimizer::stop() {
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.join();
}

double AdaptiveOptimizer::compute_sharpe(EngineType engine) {
    return m_perf.compute_score(engine);
}

// FIX #4: Quality-based throttling
void AdaptiveOptimizer::apply_quality_throttle() {
    double quality = m_latency.get_quality_ema();
    
    // If execution quality drops, tighten thresholds
    if (quality < 0.6) {
        double current_hft = m_params.hft_signal_threshold.load();
        double current_struct = m_params.structure_conf_threshold.load();
        
        m_params.hft_signal_threshold.store(
            std::min(AdaptiveParams::MAX_HFT_THRESHOLD, current_hft + 0.05));
        m_params.structure_conf_threshold.store(
            std::min(AdaptiveParams::MAX_STRUCT_THRESHOLD, current_struct + 0.05));
    }
}

void AdaptiveOptimizer::optimization_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        double hft_sharpe = compute_sharpe(EngineType::HFT);
        double struct_sharpe = compute_sharpe(EngineType::STRUCTURE);
        
        // === HFT tuning ===
        if (hft_sharpe > 0.7) {
            double current = m_params.hft_signal_threshold.load();
            m_params.hft_signal_threshold.store(
                std::max(AdaptiveParams::MIN_HFT_THRESHOLD, current - 0.05));
        }
        else if (hft_sharpe < 0.4) {
            double current = m_params.hft_signal_threshold.load();
            m_params.hft_signal_threshold.store(
                std::min(AdaptiveParams::MAX_HFT_THRESHOLD, current + 0.05));
        }
        
        // === Structure tuning ===
        if (struct_sharpe > 0.7) {
            double current = m_params.structure_conf_threshold.load();
            m_params.structure_conf_threshold.store(
                std::max(AdaptiveParams::MIN_STRUCT_THRESHOLD, current - 0.05));
        }
        else if (struct_sharpe < 0.4) {
            double current = m_params.structure_conf_threshold.load();
            m_params.structure_conf_threshold.store(
                std::min(AdaptiveParams::MAX_STRUCT_THRESHOLD, current + 0.05));
        }
        
        // === Risk tightening during drawdown ===
        if (hft_sharpe < 0.3 && struct_sharpe < 0.3) {
            double current_spread = m_params.spread_limit.load();
            double current_vol = m_params.vol_limit.load();
            
            current_spread *= 0.95;
            current_vol *= 0.9;
            
            // FIX #2: CRITICAL - Bounded parameters prevent runaway
            m_params.spread_limit.store(
                std::clamp(current_spread, AdaptiveParams::MIN_SPREAD, AdaptiveParams::MAX_SPREAD));
            m_params.vol_limit.store(
                std::clamp(current_vol, AdaptiveParams::MIN_VOL, AdaptiveParams::MAX_VOL));
        }
        
        // === Capital bias shift ===
        if (hft_sharpe > struct_sharpe + 0.2) {
            m_params.capital_bias.store(1.2);
        } else if (struct_sharpe > hft_sharpe + 0.2) {
            m_params.capital_bias.store(0.8);
        } else {
            m_params.capital_bias.store(1.0);
        }
        
        // FIX #4: Apply execution quality feedback
        apply_quality_throttle();
    }
}

} // namespace core
} // namespace chimera
