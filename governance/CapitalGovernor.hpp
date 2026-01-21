#pragma once
#include <stdexcept>
#include <string>
#include "CapitalRules.hpp"

class CapitalGovernor {
public:
    void enforce(
        const std::string& engine,
        MarketRegime regime,
        double requested_alloc,
        double requested_leverage,
        double& out_alloc,
        double& out_leverage
    ) const {
        CapitalLimits lim = limits_for_regime(regime);

        if (lim.max_alloc == 0.0 || lim.max_leverage == 0.0) {
            throw std::runtime_error(
                "CAPITAL KILL: Regime forbids capital for engine " + engine
            );
        }

        out_alloc = requested_alloc;
        out_leverage = requested_leverage;

        if (out_alloc > lim.max_alloc)
            out_alloc = lim.max_alloc;

        if (out_leverage > lim.max_leverage)
            out_leverage = lim.max_leverage;
    }
};
