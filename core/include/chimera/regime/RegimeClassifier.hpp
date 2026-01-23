#pragma once
#include "chimera/regime/MarketRegime.hpp"

struct RegimeInputs {
    double spread_bps;
    double ofi_accel;
    double volatility_bps;
};

class RegimeClassifier {
public:
    MarketRegime classify(const RegimeInputs& in) const {
        if (in.spread_bps > 8.0 && in.ofi_accel > 12.0)
            return MarketRegime::VACUUM;

        if (in.volatility_bps > 15.0 && in.ofi_accel > 8.0)
            return MarketRegime::EXPANSION;

        if (in.volatility_bps < 4.0 && in.spread_bps < 2.0)
            return MarketRegime::COMPRESSION;

        if (in.ofi_accel < -6.0)
            return MarketRegime::MEAN_REVERT;

        return MarketRegime::UNKNOWN;
    }
};
