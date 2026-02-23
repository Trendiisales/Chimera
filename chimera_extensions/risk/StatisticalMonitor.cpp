#include "StatisticalMonitor.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace chimera {
namespace risk {

StatisticalMonitor::StatisticalMonitor() {}

void StatisticalMonitor::record_pnl(double pnl) {
    m_pnl_history.push_back(pnl);
    
    if (m_pnl_history.size() > WINDOW_SIZE)
        m_pnl_history.pop_front();
}

double StatisticalMonitor::get_rolling_sharpe() const {
    if (m_pnl_history.size() < 20)
        return 0.0;
    
    double mean = std::accumulate(m_pnl_history.begin(), 
                                  m_pnl_history.end(), 0.0) 
                  / m_pnl_history.size();
    
    double variance = 0.0;
    for (double pnl : m_pnl_history) {
        double diff = pnl - mean;
        variance += diff * diff;
    }
    variance /= m_pnl_history.size();
    
    double stddev = std::sqrt(variance);
    
    if (stddev < 0.001)
        return 0.0;
    
    return mean / stddev;
}

double StatisticalMonitor::get_max_drawdown() const {
    double cumulative = 0.0;
    m_peak = 0.0;
    m_max_dd = 0.0;
    
    for (double pnl : m_pnl_history) {
        cumulative += pnl;
        
        if (cumulative > m_peak)
            m_peak = cumulative;
        
        double dd = m_peak - cumulative;
        if (dd > m_max_dd)
            m_max_dd = dd;
    }
    
    return m_max_dd;
}

bool StatisticalMonitor::is_statistical_degradation() const {
    double sharpe = get_rolling_sharpe();
    
    // GAP #8 FIX: Detect severe statistical collapse
    return sharpe < SHARPE_THRESHOLD;
}

double StatisticalMonitor::get_recommended_size_multiplier() const {
    if (!is_statistical_degradation())
        return 1.0;
    
    // Reduce size 50% when Sharpe collapses
    return 0.5;
}

} // namespace risk
} // namespace chimera
