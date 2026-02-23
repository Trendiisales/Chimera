#include "BayesianSizer.hpp"
#include <cmath>

namespace chimera {
namespace core {

BayesianSizer::BayesianSizer() {}

void BayesianSizer::record_trade(bool win) {
    if (win)
        m_alpha += 1.0;
    else
        m_beta += 1.0;
}

double BayesianSizer::get_edge_probability() const {
    return m_alpha / (m_alpha + m_beta);
}

double BayesianSizer::compute_kelly_size(double base_size, double drawdown_ratio) const {
    double edge = get_edge_probability();
    
    // Kelly formula: f = (edge * 2) - 1
    double kelly_fraction = (edge * 2.0) - 1.0;
    
    // FIX #10: CRITICAL - Apply drawdown multiplier
    // During drawdown, reduce Kelly aggressiveness
    double drawdown_multiplier = 1.0 - std::clamp(drawdown_ratio, 0.0, 0.8);
    kelly_fraction *= drawdown_multiplier;
    
    // Bound Kelly to conservative range
    kelly_fraction = std::clamp(kelly_fraction, 0.1, 1.0);
    
    return base_size * kelly_fraction;
}

} // namespace core
} // namespace chimera
