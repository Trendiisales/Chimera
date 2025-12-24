// ============================================================================
// crypto_engine/include/regime/RegimeClassifier.hpp
// ============================================================================
// Production-grade regime detection for Binance
// ============================================================================
#pragma once

#include "../signal/SignalAggregator.hpp"

enum class MarketRegime : uint8_t {
    MEAN_REVERT = 0,
    TREND = 1,
    VOLATILE = 2,
    ILLIQUID = 3,
    NEUTRAL = 4
};

class RegimeClassifier {
public:
    inline MarketRegime classify(const SignalVector& s) {
        if (abs(s.vol) > vol_thresh_)
            return MarketRegime::VOLATILE;

        if (s.tfi > trend_thresh_)
            return MarketRegime::TREND;

        if (abs(s.obi) > obi_thresh_)
            return MarketRegime::MEAN_REVERT;

        return MarketRegime::NEUTRAL;
    }

private:
    static constexpr double vol_thresh_   = 0.002;
    static constexpr double trend_thresh_ = 0.35;
    static constexpr double obi_thresh_   = 0.4;

    static inline double abs(double x) { return x < 0 ? -x : x; }
};
