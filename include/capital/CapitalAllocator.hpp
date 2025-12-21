#pragma once
#include "risk/RiskTypes.hpp"
#include "regime/RegimeTypes.hpp"
#include "capital/CapitalTypes.hpp"

namespace Chimera {

class CapitalAllocator {
public:
    CapitalAllocator(double aum);

    CapitalDecision allocate(
        const RiskDecision& risk,
        const RegimeState& regime,
        double current_drawdown,
        uint64_t ts_ns
    );

private:
    double aum_;

    double max_fraction_;
    double drawdown_limit_;

    double vol_scale_low_;
    double vol_scale_normal_;
    double vol_scale_high_;
};

}
