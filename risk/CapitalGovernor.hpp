#pragma once
#include "../config/V2Config.hpp"
#include "PortfolioRiskState.hpp"

namespace ChimeraV2 {

class CapitalGovernor {
public:
    bool approve(const PortfolioRiskState& state,
                 int total_positions,
                 int symbol_positions) const {

        if (state.daily_pnl <= -V2Config::DAILY_MAX_LOSS)
            return false;

        if (total_positions >= V2Config::MAX_CONCURRENT_TOTAL)
            return false;

        if (symbol_positions >= V2Config::MAX_CONCURRENT_PER_SYMBOL)
            return false;

        if (state.portfolio_cooldown)
            return false;

        return true;
    }
};

}
