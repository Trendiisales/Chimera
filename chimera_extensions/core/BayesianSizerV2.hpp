#pragma once

#include <algorithm>

namespace chimera {
namespace core {

// GAP #2 FIX: Accounts for trade clustering in volatile regimes
class BayesianSizerV2 {
public:
    BayesianSizerV2();
    
    // Volatility-weighted update prevents cluster bias
    void record_trade(bool win, double volatility);
    double get_edge_probability() const;
    double compute_kelly_size(double base_size, double drawdown_ratio) const;

private:
    double m_alpha = 1.0;
    double m_beta = 1.0;
};

} // namespace core
} // namespace chimera
