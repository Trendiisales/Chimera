#include "MicroPredictorV2.hpp"
#include <algorithm>

namespace chimera {
namespace engines {

MicroPredictorV2::MicroPredictorV2() {}

void MicroPredictorV2::update(double microprice) {
    // FIX #9: Track volatility for normalization
    if (!m_history.empty()) {
        double change = std::abs(microprice - m_history.back());
        m_volatility = m_volatility * 0.9 + change * 0.1;  // EMA
    }
    
    m_history.push_back(microprice);
    
    if (m_history.size() > MAX_HISTORY)
        m_history.pop_front();
}

double MicroPredictorV2::predict_drift() const {
    if (m_history.size() < 2)
        return 0.0;
    
    // Compute raw drift
    double sum = 0.0;
    for (size_t i = 1; i < m_history.size(); ++i) {
        sum += m_history[i] - m_history[i - 1];
    }
    
    double raw_drift = sum / static_cast<double>(m_history.size());
    
    // FIX #9: Normalize by volatility
    // Drift of 0.3 in high vol means less than 0.3 in low vol
    if (m_volatility < 0.001)
        return raw_drift;
    
    return raw_drift / m_volatility;
}

} // namespace engines
} // namespace chimera
