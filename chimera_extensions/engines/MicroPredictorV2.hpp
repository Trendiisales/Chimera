#pragma once

#include <deque>
#include <cmath>

namespace chimera {
namespace engines {

class MicroPredictorV2 {
public:
    MicroPredictorV2();
    
    void update(double microprice);
    double predict_drift() const;

private:
    std::deque<double> m_history;
    double m_volatility = 0.0;
    static constexpr size_t MAX_HISTORY = 20;
};

} // namespace engines
} // namespace chimera
