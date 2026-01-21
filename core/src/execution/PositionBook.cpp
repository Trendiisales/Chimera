#include "chimera/execution/PositionBook.hpp"
#include <cmath>

namespace chimera {

void PositionBook::onFill(
    const std::string& symbol,
    bool is_buy,
    double qty,
    double price
) {
    Position& pos = positions[symbol];

    double signed_qty = is_buy ? qty : -qty;
    double new_qty = pos.net_qty + signed_qty;

    if (pos.net_qty == 0.0) {
        pos.avg_price = price;
    } else {
        double weighted = (pos.avg_price * std::abs(pos.net_qty)) +
                          (price * std::abs(signed_qty));
        pos.avg_price = weighted / std::abs(new_qty);
    }

    if ((pos.net_qty > 0 && !is_buy) ||
        (pos.net_qty < 0 && is_buy)) {
        double pnl = (price - pos.avg_price) * (-signed_qty);
        pos.realized_pnl += pnl;
    }

    pos.net_qty = new_qty;
}

void PositionBook::markToMarket(
    const std::string& symbol,
    double last_price
) {
    auto it = positions.find(symbol);
    if (it == positions.end()) return;

    Position& pos = it->second;
    pos.unrealized_pnl =
        (last_price - pos.avg_price) * pos.net_qty;
}

const Position& PositionBook::get(
    const std::string& symbol
) const {
    static Position empty;
    auto it = positions.find(symbol);
    if (it == positions.end()) return empty;
    return it->second;
}

double PositionBook::totalExposure() const {
    double total = 0.0;
    for (const auto& kv : positions) {
        total += std::abs(kv.second.net_qty);
    }
    return total;
}

const std::unordered_map<std::string, Position>&
PositionBook::all() const {
    return positions;
}

void PositionBook::restore(
    const std::string& symbol,
    const Position& pos
) {
    positions[symbol] = pos;
}

}
