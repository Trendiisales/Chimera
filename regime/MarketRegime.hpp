#pragma once
#include <string>

enum class MarketRegime {
    COMPRESSION,
    EXPANSION,
    VACUUM,
    ABSORPTION,
    MEAN_REVERT,
    UNKNOWN
};

inline const char* to_string(MarketRegime r) {
    switch (r) {
        case MarketRegime::COMPRESSION: return "COMPRESSION";
        case MarketRegime::EXPANSION: return "EXPANSION";
        case MarketRegime::VACUUM: return "VACUUM";
        case MarketRegime::ABSORPTION: return "ABSORPTION";
        case MarketRegime::MEAN_REVERT: return "MEAN_REVERT";
        default: return "UNKNOWN";
    }
}
