#pragma once
#include <string>
#include <stdexcept>
#include "MarketRegime.hpp"

inline void enforce_engine_regime(
    const std::string& engine,
    MarketRegime regime
) {
    bool allowed = false;

    if (engine == "FADE") {
        allowed = (regime == MarketRegime::COMPRESSION ||
                   regime == MarketRegime::MEAN_REVERT ||
                   regime == MarketRegime::ABSORPTION);
    }

    if (engine == "CASCADE") {
        allowed = (regime == MarketRegime::EXPANSION ||
                   regime == MarketRegime::VACUUM);
    }

    if (engine == "MOMENTUM") {
        allowed = (regime == MarketRegime::EXPANSION);
    }

    if (!allowed) {
        throw std::runtime_error(
            "REGIME VIOLATION: Engine '" + engine +
            "' not allowed in regime " + to_string(regime)
        );
    }
}
