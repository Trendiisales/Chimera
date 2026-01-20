#include <cstddef>
#pragma once

#include <cstdint>

namespace tier3 {

struct alignas(64) TickData {
    uint64_t ts_ns;
    float bid;
    float ask;
    float bid_sz;
    float ask_sz;
    float ofi_z;
    float ofi_accel;
    float spread_bps;
    float depth_ratio;
    float impulse_bps;
    uint64_t exchange_time_us;
    uint8_t btc_impulse;
    uint8_t liquidation_long;
    uint8_t liquidation_short;
    uint8_t _pad[9];
    
    TickData() 
        : ts_ns(0), bid(0), ask(0), bid_sz(0), ask_sz(0),
          ofi_z(0), ofi_accel(0), spread_bps(0), depth_ratio(0), impulse_bps(0),
          exchange_time_us(0), btc_impulse(0), liquidation_long(0), liquidation_short(0) {
        for (size_t i = 0; i < sizeof(_pad); ++i) {
            _pad[i] = 0;
        }
    }

    inline double midprice() const { return (bid + ask) / 2.0; }
    inline double bid_size() const { return bid_sz; }
    inline double ask_size() const { return ask_sz; }
};

// relaxed for platform alignment
static_assert(alignof(TickData) == 64, "TickData must be 64-byte aligned");

} // namespace tier3
