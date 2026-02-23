#pragma once

#include <algorithm>

namespace chimera {
namespace execution {

// GAP #3 FIX: Accounts for tape velocity, not just static book
class QueueModelV2 {
public:
    QueueModelV2();
    
    void update(double top_size, double traded_volume);
    double fill_probability() const;

private:
    double m_book_size = 0.0;
    double m_trade_velocity = 0.0;
};

} // namespace execution
} // namespace chimera
