#pragma once

#include <deque>

namespace chimera {
namespace engines {

// GAP #4 FIX: Dynamic beta instead of fixed 100x multiplier
class MetalCorrelationModelV2 {
public:
    MetalCorrelationModelV2();
    
    void update(double xau_price, double xag_price);
    double get_spread_z_score() const;
    double get_beta() const { return m_beta; }

private:
    double m_beta = 100.0;  // Initialize with typical ratio
    double m_cov = 0.0;
    double m_var = 0.0;
    
    std::deque<double> m_spread_history;
    static constexpr size_t MAX_HISTORY = 100;
    static constexpr double EMA_ALPHA = 0.01;
};

} // namespace engines
} // namespace chimera
