#include "chimera/safety/PrecisionEngine.hpp"
#include <cmath>

namespace chimera {

PrecisionEngine::PrecisionEngine(
    ExchangeInfoCache& cache
) : exinfo(cache) {}

double PrecisionEngine::roundDown(
    double v,
    double step
) const {
    if (step <= 0.0) return v;
    return std::floor(v / step) * step;
}

bool PrecisionEngine::normalize(
    const std::string& symbol,
    double& qty,
    double& price
) const {
    if (!exinfo.has(symbol)) {
        return false;
    }

    const SymbolRules& r =
        exinfo.rules(symbol);

    qty = roundDown(qty, r.step_size);
    price = roundDown(price, r.tick_size);

    if (qty < r.min_qty) {
        return false;
    }

    if (qty * price < r.min_notional) {
        return false;
    }

    return true;
}

}
