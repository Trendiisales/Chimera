#pragma once

#include <algorithm>

namespace chimera {
namespace core {

class BayesianSizer {
public:
    BayesianSizer();
    
    void record_trade(bool win);
    double get_edge_probability() const;
    
    // FIX #10: Kelly fraction with drawdown protection
    double compute_kelly_size(double base_size, double drawdown_ratio) const;

private:
    double m_alpha = 1.0;
    double m_beta = 1.0;
};

} // namespace core
} // namespace chimera
