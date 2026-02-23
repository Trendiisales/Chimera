#include "QueueModelV2.hpp"

namespace chimera {
namespace execution {

QueueModelV2::QueueModelV2() {}

void QueueModelV2::update(double top_size, double traded_volume) {
    m_book_size = top_size;
    m_trade_velocity = traded_volume;
}

double QueueModelV2::fill_probability() const {
    if (m_book_size <= 0.0)
        return 0.0;
    
    // GAP #3 FIX: Use velocity relative to book depth
    // Fast tape = higher fill probability
    double velocity_factor = m_trade_velocity / m_book_size;
    
    return std::clamp(velocity_factor, 0.0, 1.0);
}

} // namespace execution
} // namespace chimera
