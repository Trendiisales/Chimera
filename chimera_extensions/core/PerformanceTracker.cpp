#include "PerformanceTracker.hpp"
#include <algorithm>
#include <cmath>

namespace chimera {
namespace core {

PerformanceTracker::PerformanceTracker() {
    m_metrics[EngineType::HFT] = EngineMetrics{};
    m_metrics[EngineType::STRUCTURE] = EngineMetrics{};
}

void PerformanceTracker::record_fill(EngineType engine, double pnl, double latency, double slippage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto& m = m_metrics[engine];
    m.pnl += pnl;
    m.trades++;
    
    if (pnl > 0)
        m.wins++;
    
    m.avg_latency = (m.avg_latency * 0.9) + (latency * 0.1);
    m.avg_slippage = (m.avg_slippage * 0.9) + (std::abs(slippage) * 0.1);
    
    recalc_win_rate(m);
}

void PerformanceTracker::recalc_win_rate(EngineMetrics& m) {
    if (m.trades == 0)
        m.win_rate = 0.0;
    else
        m.win_rate = static_cast<double>(m.wins) / m.trades;
}

double PerformanceTracker::compute_score(EngineType engine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& m = m_metrics[engine];
    
    double pnl_score = std::tanh(m.pnl / 1000.0);
    double win_score = m.win_rate;
    double latency_score = 1.0 - std::min(m.avg_latency / 100.0, 1.0);
    double slip_score = 1.0 - std::min(m.avg_slippage / 1.0, 1.0);
    
    double total = (pnl_score * 0.4) + (win_score * 0.2) + (latency_score * 0.2) + (slip_score * 0.2);
    return std::clamp(total, 0.0, 1.0);
}

double PerformanceTracker::get_allocation_weight(EngineType engine) {
    double hft_score = compute_score(EngineType::HFT);
    double struct_score = compute_score(EngineType::STRUCTURE);
    double total = hft_score + struct_score;
    
    if (total == 0.0)
        return 0.5;
    
    if (engine == EngineType::HFT)
        return hft_score / total;
    else
        return struct_score / total;
}

const EngineMetrics& PerformanceTracker::get_metrics(EngineType engine) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metrics.at(engine);
}

} // namespace core
} // namespace chimera
