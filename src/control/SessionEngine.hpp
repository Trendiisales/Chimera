#pragma once
#include <string>
#include <cstdint>

namespace chimera {

enum class MarketSession {
    ASIA = 0,
    EU = 1,
    US = 2,
    ROLLOVER = 3
};

class SessionEngine {
public:
    SessionEngine();

    MarketSession session(uint64_t ts_ns) const;

    bool trading_allowed(uint64_t ts_ns) const;
    double size_multiplier(uint64_t ts_ns) const;

    const char* name(MarketSession s) const;

private:
    static constexpr int ROLLOVER_START = 23; // UTC hour
    static constexpr int ROLLOVER_END   = 0;  // UTC hour

    int hour_from_ns(uint64_t ts_ns) const;
};

}
