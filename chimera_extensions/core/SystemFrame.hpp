#pragma once

#include "../core/OrderIntentTypes.hpp"

namespace chimera {
namespace core {

// FIX #7: Coherent snapshot eliminates atomic drift
struct SystemFrame {
    double spread = 0.0;
    double volatility = 0.0;
    double latency = 0.0;
    double hft_score = 0.0;
    double structure_score = 0.0;
    double quality_score = 0.0;
    double global_exposure = 0.0;
    double hft_exposure = 0.0;
    double structure_exposure = 0.0;
    double daily_pnl = 0.0;
    uint64_t timestamp_ns = 0;
    bool lockdown_active = false;
};

} // namespace core
} // namespace chimera
