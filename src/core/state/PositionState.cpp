#include "state/PositionState.hpp"
#include <cmath>

namespace chimera {

PositionState::PositionState() : m_equity(0.0) {}

void PositionState::onFill(const std::string& symbol,
                            const std::string&,
                            double price,
                            double qty,
                            double fee,
                            uint64_t) {
    auto& pos = m_positions[symbol];

    double new_qty = pos.net_qty + qty;

    if (pos.net_qty != 0.0 && (pos.net_qty > 0) != (qty > 0)) {
        double closed = std::min(std::abs(qty), std::abs(pos.net_qty));
        double pnl = closed * (price - pos.avg_price) * (pos.net_qty > 0 ? 1.0 : -1.0);
        pos.realized_pnl += pnl;
        m_equity.fetch_add(pnl, std::memory_order_relaxed);
    }

    if (new_qty != 0.0) {
        pos.avg_price =
            ((pos.avg_price * pos.net_qty) + (price * qty)) / new_qty;
    }

    pos.net_qty = new_qty;
    pos.fees += fee;
    m_equity.fetch_sub(fee, std::memory_order_relaxed);
}

PositionSnapshot PositionState::snapshot(const std::string& symbol) const {
    PositionSnapshot snap;
    auto it = m_positions.find(symbol);
    if (it == m_positions.end()) return snap;

    const auto& pos = it->second;
    snap.net_qty = pos.net_qty;
    snap.avg_price = pos.avg_price;
    snap.realized_pnl = pos.realized_pnl;
    snap.fees = pos.fees;
    return snap;
}

double PositionState::totalEquity() const {
    return m_equity.load(std::memory_order_relaxed);
}

}
