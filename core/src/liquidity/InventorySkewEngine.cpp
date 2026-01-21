#include "chimera/liquidity/InventorySkewEngine.hpp"
#include <algorithm>
#include <cmath>

namespace chimera {

InventorySkewEngine::InventorySkewEngine(
    PositionBook& book
) : position_book(book) {}

void InventorySkewEngine::setMaxSkewBps(double bps) {
    max_skew_bps = bps;
}

void InventorySkewEngine::setSkewPerUnit(double bps) {
    skew_per_unit = bps;
}

double InventorySkewEngine::skewBps(
    const std::string& symbol
) const {
    const Position& pos =
        position_book.get(symbol);

    double exposure =
        std::abs(pos.net_qty);

    double skew =
        exposure * skew_per_unit;

    return std::min(
        skew,
        max_skew_bps
    );
}

}
