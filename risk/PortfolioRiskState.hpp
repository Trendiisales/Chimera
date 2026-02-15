#pragma once
#include <cstdint>

namespace ChimeraV2 {

struct PortfolioRiskState {
    double daily_pnl = 0.0;
    int consecutive_losses = 0;
    bool portfolio_cooldown = false;
    uint64_t cooldown_end_ns = 0;
};

}
