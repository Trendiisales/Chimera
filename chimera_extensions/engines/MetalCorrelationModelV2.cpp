#include "MetalCorrelationModelV2.hpp"
#include <numeric>
#include <cmath>
#include <algorithm>

namespace chimera {
namespace engines {

MetalCorrelationModelV2::MetalCorrelationModelV2() {}

void MetalCorrelationModelV2::update(double xau, double xag) {
    // GAP #4 FIX: Rolling beta estimation
    // Adapts to changing correlation regime
    
    m_cov = (1.0 - EMA_ALPHA) * m_cov + EMA_ALPHA * (xau * xag);
    m_var = (1.0 - EMA_ALPHA) * m_var + EMA_ALPHA * (xag * xag);
    
    if (m_var > 0.001) {
        m_beta = m_cov / m_var;
        // Bound beta to reasonable range
        m_beta = std::clamp(m_beta, 50.0, 150.0);
    }
    
    // Compute spread using dynamic beta
    double spread = xau - (m_beta * xag);
    
    m_spread_history.push_back(spread);
    if (m_spread_history.size() > MAX_HISTORY)
        m_spread_history.pop_front();
}

double MetalCorrelationModelV2::get_spread_z_score() const {
    if (m_spread_history.size() < 20)
        return 0.0;
    
    double mean = std::accumulate(m_spread_history.begin(), 
                                  m_spread_history.end(), 0.0) 
                  / m_spread_history.size();
    
    double variance = 0.0;
    for (double s : m_spread_history) {
        double diff = s - mean;
        variance += diff * diff;
    }
    variance /= m_spread_history.size();
    
    double stddev = std::sqrt(variance);
    
    if (stddev < 0.001)
        return 0.0;
    
    double current_spread = m_spread_history.back();
    return (current_spread - mean) / stddev;
}

} // namespace engines
} // namespace chimera
