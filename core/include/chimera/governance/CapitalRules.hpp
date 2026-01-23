#pragma once
#include "chimera/regime/MarketRegime.hpp"

struct CapitalLimits {
    double max_leverage;
    double max_alloc;
};

inline CapitalLimits limits_for_regime(MarketRegime r) {
    switch (r) {
        case MarketRegime::COMPRESSION:
            return {2.0, 0.25};
        case MarketRegime::EXPANSION:
            return {3.0, 0.40};
        case MarketRegime::VACUUM:
            return {1.5, 0.15};
        case MarketRegime::ABSORPTION:
            return {1.0, 0.10};
        case MarketRegime::MEAN_REVERT:
            return {2.0, 0.25};
        default:
            return {0.0, 0.0};
    }
}
