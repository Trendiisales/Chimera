#pragma once
#include "../../engines/StopRunDetector.hpp"
#include "../../engines/LiquidityVacuum.hpp"
#include "../../engines/SessionBias.hpp"

namespace chimera {

enum class TradeIntent {
    HOLD = 0,
    LONG = 1,
    SHORT = 2
};

class SignalFusion {
public:
    TradeIntent fuse(StopRunState stop,
                      VacuumState vacuum,
                      double bias);
};

}
