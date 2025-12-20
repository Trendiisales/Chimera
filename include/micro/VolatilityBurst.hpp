#pragma once
#include "micro/MicroSignal.hpp"

namespace Chimera {

class VolatilityBurst {
public:
    VolatilityBurst();

    MicroSignal on_price(double price, uint64_t ts_ns);

private:
    double last_px_;
    double ema_var_;
};

}
