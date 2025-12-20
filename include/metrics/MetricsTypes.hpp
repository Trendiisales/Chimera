#pragma once
#include <cstdint>

namespace Chimera {

struct alignas(64) MetricsSnapshot {
    uint64_t ts_ns;

    uint64_t binance_ticks;
    uint64_t fix_execs;

    uint64_t exec_allowed;
    uint64_t exec_blocked;

    uint64_t divergences;
    uint64_t alerts_critical;
};

}
