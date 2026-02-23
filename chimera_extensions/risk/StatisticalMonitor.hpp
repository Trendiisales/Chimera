#pragma once

#include <deque>
#include <atomic>

namespace chimera {
namespace risk {

// GAP #8 FIX: Detects statistical performance collapse
class StatisticalMonitor {
public:
    StatisticalMonitor();
    
    void record_pnl(double pnl);
    double get_rolling_sharpe() const;
    double get_max_drawdown() const;
    
    bool is_statistical_degradation() const;
    double get_recommended_size_multiplier() const;

private:
    std::deque<double> m_pnl_history;
    mutable double m_peak = 0.0;
    mutable double m_max_dd = 0.0;
    
    static constexpr size_t WINDOW_SIZE = 100;
    static constexpr double SHARPE_THRESHOLD = -1.5;
};

} // namespace risk
} // namespace chimera
