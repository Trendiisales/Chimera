#include "accounting/DailyPnlStore.hpp"
#include "execution/Fill.hpp"

void DailyPnlStore::on_fill(const Fill& f) {
    double delta =
        (f.side == "SELL")
            ? (f.price * f.qty)
            : -(f.price * f.qty);

    pnl_ += delta;
}
