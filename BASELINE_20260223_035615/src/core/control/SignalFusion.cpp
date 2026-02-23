#include "SignalFusion.hpp"

namespace chimera {

TradeIntent SignalFusion::fuse(StopRunState stop,
                                VacuumState vacuum,
                                double bias) {
    if (vacuum == VacuumState::VACUUM) {
        if (stop == StopRunState::UP && bias > 0.8) return TradeIntent::LONG;
        if (stop == StopRunState::DOWN && bias > 0.8) return TradeIntent::SHORT;
    }
    return TradeIntent::HOLD;
}

}
