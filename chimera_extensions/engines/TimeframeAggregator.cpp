#include "TimeframeAggregator.hpp"

namespace chimera {
namespace engines {

TimeframeAggregator::TimeframeAggregator(int seconds)
    : m_timeframe_seconds(seconds)
    , m_start(std::chrono::steady_clock::now()) {}

void TimeframeAggregator::update(double price) {
    auto now = std::chrono::steady_clock::now();
    
    if (std::chrono::duration_cast<std::chrono::seconds>(now - m_start).count() >= m_timeframe_seconds) {
        m_prices.clear();
        m_start = now;
    }
    
    m_prices.push_back(price);
}

bool TimeframeAggregator::is_ready() const {
    return m_prices.size() > 5;
}

Candle TimeframeAggregator::get_latest_candle() const {
    Candle c;
    c.open = m_prices.front();
    c.close = m_prices.back();
    c.high = *std::max_element(m_prices.begin(), m_prices.end());
    c.low = *std::min_element(m_prices.begin(), m_prices.end());
    return c;
}

} // namespace engines
} // namespace chimera
