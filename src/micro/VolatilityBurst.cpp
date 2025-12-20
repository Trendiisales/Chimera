#include "micro/VolatilityBurst.hpp"
#include <cmath>

using namespace Chimera;

VolatilityBurst::VolatilityBurst()
    : last_px_(0.0), ema_var_(0.0) {}

MicroSignal VolatilityBurst::on_price(double price, uint64_t ts_ns) {
    if (last_px_ == 0.0) {
        last_px_ = price;
        return {0.0, ts_ns};
    }

    double ret = price - last_px_;
    last_px_ = price;

    ema_var_ = 0.9 * ema_var_ + 0.1 * (ret * ret);
    double vol = std::sqrt(ema_var_);

    return {vol, ts_ns};
}
