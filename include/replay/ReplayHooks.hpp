#pragma once
#include <cstdint>

namespace Chimera {

// These are INTENTIONAL weak hooks.
// Your live engine already has equivalents.
// Replay will call these. Live mode will not.

void replay_on_binance_tick(
    uint64_t ts_exchange,
    double bid,
    double ask,
    double bid_qty,
    double ask_qty
);

void replay_on_fix_execution(
    uint64_t cl_ord_id,
    uint64_t ts_exchange,
    double price,
    double qty,
    uint8_t side
);

}
