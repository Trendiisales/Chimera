#include "replay/ReplayHooks.hpp"

namespace Chimera {

// Default no-op implementations.
// Engine can override by linking its own definitions.

void replay_on_binance_tick(
    uint64_t,
    double,
    double,
    double,
    double
) {}

void replay_on_fix_execution(
    uint64_t,
    uint64_t,
    double,
    double,
    uint8_t
) {}

}
