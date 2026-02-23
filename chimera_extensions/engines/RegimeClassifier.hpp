#pragma once

#include "TimeframeAggregator.hpp"

namespace chimera {
namespace engines {

enum class RegimeType {
    TREND_UP,
    TREND_DOWN,
    COMPRESSION,
    BREAKOUT,
    MEAN_REVERT
};

struct RegimeSignal {
    RegimeType regime = RegimeType::COMPRESSION;
    double confidence = 0.0;
};

class RegimeClassifier {
public:
    RegimeClassifier();
    
    void update(double price);
    RegimeSignal classify();

private:
    double compute_atr(const Candle& c);

    TimeframeAggregator m_1min;
    TimeframeAggregator m_5min;
};

} // namespace engines
} // namespace chimera
