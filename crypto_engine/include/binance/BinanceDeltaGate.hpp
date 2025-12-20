#pragma once
#include "BinanceTypes.hpp"

namespace binance {

enum class DeltaResult {
    DROP_OLD,
    ACCEPT,
    GAP
};

class DeltaGate {
    uint64_t last_u = 0;
    bool initialized = false;

public:
    void reset(uint64_t snapshot_lastUpdateId) {
        last_u = snapshot_lastUpdateId;
        initialized = true;
    }

    DeltaResult evaluate(const DepthDelta& d) {
        if (!initialized)
            return DeltaResult::GAP;

        if (d.u <= last_u)
            return DeltaResult::DROP_OLD;

        if (d.U > last_u + 1)
            return DeltaResult::GAP;

        last_u = d.u;
        return DeltaResult::ACCEPT;
    }
};

}
