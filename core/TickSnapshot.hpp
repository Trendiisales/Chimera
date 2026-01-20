#pragma once
#include <cstdint>
#include <string>

struct TickSnapshot {
    uint64_t ts_ns = 0;
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double ofi_accel = 0.0;
    double spread_bps = 0.0;
    double depth_ratio = 0.0;
    double impulse_bps = 0.0;
    uint64_t hold_ms = 0;
    std::string symbol;
};
