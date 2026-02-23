#include "BayesianSizerV2.hpp"
#include <cmath>

namespace chimera {
namespace core {

BayesianSizerV2::BayesianSizerV2() {}

void BayesianSizerV2::record_trade(bool win, double volatility) {
    // GAP #2 FIX: Weight by inverse volatility
    // High-vol clusters count less (prevent streak bias)
    double weight = std::clamp(1.0 / std::max(volatility, 0.1), 0.2, 2.0);
    
    if (win)
        m_alpha += weight;
    else
        m_beta += weight;
}

double BayesianSizerV2::get_edge_probability() const {
    return m_alpha / (m_alpha + m_beta);
}

double BayesianSizerV2::compute_kelly_size(double base_size, double drawdown_ratio) const {
    double edge = get_edge_probability();
    
    double kelly_fraction = (edge * 2.0) - 1.0;
    
    // Apply drawdown protection
    double drawdown_multiplier = 1.0 - std::clamp(drawdown_ratio, 0.0, 0.8);
    kelly_fraction *= drawdown_multiplier;
    
    kelly_fraction = std::clamp(kelly_fraction, 0.1, 1.0);
    
    return base_size * kelly_fraction;
}

} // namespace core
} // namespace chimera
