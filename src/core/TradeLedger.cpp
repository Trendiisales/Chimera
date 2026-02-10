#include "core/TradeLedger.hpp"

TradeLedger::TradeLedger() : next_trade_id_(1) {}

uint64_t TradeLedger::open_trade(const std::string& symbol, TradeSide side, double size, double entry_price, uint64_t ts) {
    TradeLedgerEntry entry{};
    entry.trade_id = next_trade_id_++;
    entry.symbol = symbol;
    entry.side = side;
    entry.size = size;
    entry.entry_price = entry_price;
    entry.exit_price = entry_price;
    entry.entry_ts = ts;
    entry.exit_ts = 0;
    entry.gross_pnl = 0.0;
    entry.costs = {0.0, 0.0, 0.0, 0.0};
    entry.net_pnl = 0.0;
    index_[entry.trade_id] = trades_.size();
    trades_.push_back(entry);
    return entry.trade_id;
}

void TradeLedger::close_trade(uint64_t trade_id, double exit_price, uint64_t ts) {
    auto it = index_.find(trade_id);
    if (it == index_.end()) return;
    auto& t = trades_[it->second];
    t.exit_price = exit_price;
    t.exit_ts = ts;
    double direction = (t.side == TradeSide::BUY) ? 1.0 : -1.0;
    t.gross_pnl = (t.exit_price - t.entry_price) * direction * t.size;
    t.net_pnl = t.gross_pnl - t.costs.total_cost;
}

void TradeLedger::apply_costs(uint64_t trade_id, double spread_cost, double commission_cost, double latency_cost) {
    auto it = index_.find(trade_id);
    if (it == index_.end()) return;
    auto& t = trades_[it->second];
    t.costs.spread_cost = spread_cost;
    t.costs.commission_cost = commission_cost;
    t.costs.latency_cost = latency_cost;
    t.costs.total_cost = spread_cost + commission_cost + latency_cost;
    if (t.exit_ts != 0) {
        t.net_pnl = t.gross_pnl - t.costs.total_cost;
    }
}

const std::vector<TradeLedgerEntry>& TradeLedger::all_trades() const { return trades_; }
double TradeLedger::total_net_pnl() const {
    double sum = 0.0;
    for (const auto& t : trades_) sum += t.net_pnl;
    return sum;
}
double TradeLedger::total_gross_pnl() const {
    double sum = 0.0;
    for (const auto& t : trades_) sum += t.gross_pnl;
    return sum;
}
