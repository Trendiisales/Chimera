#pragma once

#include <deque>
#include <chrono>
#include <algorithm>

namespace chimera {
namespace engines {

struct Candle {
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

class TimeframeAggregator {
public:
    explicit TimeframeAggregator(int seconds);
    
    void update(double price);
    bool is_ready() const;
    Candle get_latest_candle() const;

private:
    int m_timeframe_seconds;
    std::deque<double> m_prices;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace engines
} // namespace chimera
