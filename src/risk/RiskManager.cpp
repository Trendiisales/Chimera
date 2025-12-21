#include "risk/RiskManager.hpp"
#include "execution/Fill.hpp"

void RiskManager::on_fill(const Fill& f) {
    double delta =
        (f.side == "SELL")
            ? (f.price * f.qty)
            : -(f.price * f.qty);

    pnl_ += delta;
}
