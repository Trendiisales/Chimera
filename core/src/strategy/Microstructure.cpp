#include "chimera/strategy/Microstructure.hpp"
#include <cmath>

namespace chimera {

void Microstructure::onTick(
    const std::string& symbol,
    double bid,
    double ask,
    double bid_sz,
    double ask_sz,
    uint64_t ts_ns
) {
    OFIState& ofi = ofi_map[symbol];
    double delta = (bid_sz - ask_sz);

    if (ofi.ema == 0.0) {
        ofi.ema = delta;
    } else {
        ofi.ema = (ema_alpha * delta) +
                  ((1.0 - ema_alpha) * ofi.ema);
    }

    ofi.last_bid = bid;
    ofi.last_ask = ask;

    ImpulseState& imp = impulse_map[symbol];
    if (imp.last_price != 0.0) {
        double bps =
            std::abs((ask - imp.last_price) /
                     imp.last_price) * 10000.0;

        if (bps >= impulse_bps) {
            imp.open = true;
            imp.last_ts = ts_ns;
        }
    }

    if (imp.open &&
        ts_ns - imp.last_ts > impulse_ttl_ns) {
        imp.open = false;
    }

    imp.last_price = ask;
}

double Microstructure::ofi(
    const std::string& symbol
) const {
    auto it = ofi_map.find(symbol);
    if (it == ofi_map.end()) return 0.0;
    return it->second.ema;
}

bool Microstructure::impulseOpen(
    const std::string& symbol
) const {
    auto it = impulse_map.find(symbol);
    if (it == impulse_map.end()) return false;
    return it->second.open;
}

}
