#pragma once

#include <cstdint>
#include <string>

namespace chimera {
namespace telemetry {

struct DeskSnapshot {
    double global_exposure = 0.0;
    double hft_exposure = 0.0;
    double structure_exposure = 0.0;
    double daily_pnl = 0.0;
    double latency_ema = 0.0;
    double slippage_ema = 0.0;
    double hft_score = 0.0;
    double structure_score = 0.0;
    double hft_threshold = 0.0;
    double structure_threshold = 0.0;
    double spread_limit = 0.0;
    double vol_limit = 0.0;
    bool lockdown_mode = false;
    uint64_t timestamp_ns = 0;
    int total_trades = 0;
};

} // namespace telemetry
} // namespace chimera
