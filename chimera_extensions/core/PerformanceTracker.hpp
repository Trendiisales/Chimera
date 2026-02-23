#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include "../core/OrderIntentTypes.hpp"

namespace chimera {
namespace core {

struct EngineMetrics {
    double pnl = 0.0;
    double win_rate = 0.0;
    double avg_latency = 0.0;
    double avg_slippage = 0.0;
    int trades = 0;
    int wins = 0;
};

class PerformanceTracker {
public:
    PerformanceTracker();

    void record_fill(EngineType engine, double pnl, double latency, double slippage);
    double compute_score(EngineType engine);
    double get_allocation_weight(EngineType engine);
    const EngineMetrics& get_metrics(EngineType engine) const;

private:
    void recalc_win_rate(EngineMetrics& m);

    std::unordered_map<EngineType, EngineMetrics> m_metrics;
    mutable std::mutex m_mutex;
};

} // namespace core
} // namespace chimera
