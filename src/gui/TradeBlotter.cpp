#include "TradeBlotter.hpp"
#include <chrono>

TradeBlotter g_blotter;

void TradeBlotter::addEntry(uint64_t id, const std::string& sym, double qty, double px) {
    TradeRow r{};
    r.id = id;
    r.symbol = sym;
    r.qty = qty;
    r.entry_px = px;
    r.exit_px = 0.0;
    r.fees = 0.0;
    r.pnl = 0.0;
    r.ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    rows_.push_back(r);
}

void TradeBlotter::addExit(uint64_t id, double px, double fees) {
    for (auto& r : rows_) {
        if (r.id == id && r.exit_px == 0.0) {
            r.exit_px = px;
            r.fees = fees;
            r.pnl = (px - r.entry_px) * r.qty - fees;
            return;
        }
    }
}

std::vector<TradeRow> TradeBlotter::snapshot() const {
    return rows_;
}
