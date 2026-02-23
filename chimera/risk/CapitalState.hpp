#pragma once
#include <cstdint>
#include "CapitalTypes.hpp"

namespace chimera {

struct CapitalState {
    RiskMode mode = RiskMode::NORMAL;

    double global_multiplier = 1.0;
    double max_symbol_exposure = 1.0;
    double max_engine_exposure = 1.0;

    double realized_pnl = 0.0;
    double drawdown = 0.0;

    uint64_t last_update_ns = 0;
};

}
