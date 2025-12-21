#include "accounting/PositionTracker.hpp"

void PositionTracker::on_fill(const std::string& symbol,
                              const std::string& side,
                              double price,
                              double qty) {
    Position& p = positions_[symbol];

    if (side == "BUY") {
        double notional = p.qty * p.avg_price + qty * price;
        p.qty += qty;
        p.avg_price = (p.qty != 0.0) ? notional / p.qty : 0.0;
    } else {
        double closed = std::min(qty, p.qty);
        realized_ += closed * (price - p.avg_price);
        p.qty -= closed;
        if (p.qty == 0.0) p.avg_price = 0.0;
    }
}

double PositionTracker::realized_pnl() const {
    return realized_;
}

double PositionTracker::unrealized_pnl(const std::string& symbol, double mid) const {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return 0.0;
    return it->second.qty * (mid - it->second.avg_price);
}

double PositionTracker::total_unrealized(
    const std::unordered_map<std::string,double>& mids
) const {
    double total = 0.0;
    for (const auto& kv : mids) {
        total += unrealized_pnl(kv.first, kv.second);
    }
    return total;
}
