#include "control/SessionEngine.hpp"

namespace chimera {

SessionEngine::SessionEngine() {}

int SessionEngine::hour_from_ns(uint64_t ts_ns) const {
    uint64_t sec = ts_ns / 1'000'000'000ULL;
    uint64_t hour = (sec / 3600ULL) % 24ULL;
    return static_cast<int>(hour);
}

MarketSession SessionEngine::session(uint64_t ts_ns) const {
    int h = hour_from_ns(ts_ns);

    if (h == ROLLOVER_START || h == ROLLOVER_END)
        return MarketSession::ROLLOVER;

    if (h >= 0 && h < 7)
        return MarketSession::ASIA;

    if (h >= 7 && h < 13)
        return MarketSession::EU;

    return MarketSession::US;
}

bool SessionEngine::trading_allowed(uint64_t ts_ns) const {
    // Crypto markets trade 24/7 - no rollover blocking
    return true;
}

double SessionEngine::size_multiplier(uint64_t ts_ns) const {
    MarketSession s = session(ts_ns);

    switch (s) {
        case MarketSession::ASIA:
            return 0.25;  // Scout only
        case MarketSession::EU:
            return 0.5;   // Half size
        case MarketSession::US:
            return 1.0;   // Full expansion
        case MarketSession::ROLLOVER:
        default:
            return 0.0;   // No trading
    }
}

const char* SessionEngine::name(MarketSession s) const {
    switch (s) {
        case MarketSession::ASIA: return "ASIA";
        case MarketSession::EU: return "EU";
        case MarketSession::US: return "US";
        case MarketSession::ROLLOVER: return "ROLLOVER";
        default: return "UNKNOWN";
    }
}

}
