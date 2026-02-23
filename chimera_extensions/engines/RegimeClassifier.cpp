#include "RegimeClassifier.hpp"
#include <cmath>

namespace chimera {
namespace engines {

RegimeClassifier::RegimeClassifier() : m_1min(60), m_5min(300) {}

void RegimeClassifier::update(double price) {
    m_1min.update(price);
    m_5min.update(price);
}

double RegimeClassifier::compute_atr(const Candle& c) {
    return c.high - c.low;
}

RegimeSignal RegimeClassifier::classify() {
    RegimeSignal signal;
    
    if (!m_1min.is_ready() || !m_5min.is_ready())
        return signal;
    
    Candle c1 = m_1min.get_latest_candle();
    Candle c5 = m_5min.get_latest_candle();
    
    double atr1 = compute_atr(c1);
    double atr5 = compute_atr(c5);
    
    // Compression detection
    if (atr1 < atr5 * 0.3) {
        signal.regime = RegimeType::COMPRESSION;
        signal.confidence = 0.7;
        return signal;
    }
    
    // Breakout detection
    if (c1.close > c5.high) {
        signal.regime = RegimeType::BREAKOUT;
        signal.confidence = 0.9;
        return signal;
    }
    
    // Trend up
    if (c1.close > c1.open && c1.close > c5.close) {
        signal.regime = RegimeType::TREND_UP;
        signal.confidence = 0.6;
        return signal;
    }
    
    // Trend down
    if (c1.close < c1.open && c1.close < c5.close) {
        signal.regime = RegimeType::TREND_DOWN;
        signal.confidence = 0.6;
        return signal;
    }
    
    signal.regime = RegimeType::MEAN_REVERT;
    signal.confidence = 0.5;
    return signal;
}

} // namespace engines
} // namespace chimera
