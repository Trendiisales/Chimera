#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace chimera {

struct OFIState {
    double ema = 0.0;
    double last_bid = 0.0;
    double last_ask = 0.0;
};

struct ImpulseState {
    double last_price = 0.0;
    uint64_t last_ts = 0;
    bool open = false;
};

class Microstructure {
public:
    void onTick(
        const std::string& symbol,
        double bid,
        double ask,
        double bid_sz,
        double ask_sz,
        uint64_t ts_ns
    );

    double ofi(const std::string& symbol) const;
    bool impulseOpen(const std::string& symbol) const;

private:
    std::unordered_map<std::string, OFIState> ofi_map;
    std::unordered_map<std::string, ImpulseState> impulse_map;

    double ema_alpha = 0.2;
    double impulse_bps = 12.0;
    uint64_t impulse_ttl_ns = 300000000ULL;
};

}
