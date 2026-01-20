#include "PositionTracker.hpp"
#include <cmath>

void PositionTracker::onFill(const std::string& sym, double qty, double price) {
    std::lock_guard<std::mutex> lock(mtx);
    auto& p = positions[sym];

    double new_qty = p.qty + qty;

    if (p.qty != 0 && (p.qty > 0) != (new_qty > 0)) {
        double closed = std::min(std::abs(qty), std::abs(p.qty));
        p.realized_pnl += closed * (price - p.avg_price) * (p.qty > 0 ? 1 : -1);
    }

    if (p.qty + qty != 0)
        p.avg_price = (p.avg_price * p.qty + price * qty) / (p.qty + qty);

    p.qty = new_qty;
}

double PositionTracker::unrealized(const std::string& sym, double mark) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = positions.find(sym);
    if (it == positions.end()) return 0;
    return it->second.qty * (mark - it->second.avg_price);
}

Position PositionTracker::get(const std::string& sym) {
    std::lock_guard<std::mutex> lock(mtx);
    return positions[sym];
}
