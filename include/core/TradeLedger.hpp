#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

enum class TradeSide { BUY, SELL };

struct TradeCostBreakdown {
    double spread_cost;
    double commission_cost;
    double latency_cost;
    double total_cost;
};

struct TradeLedgerEntry {
    uint64_t trade_id;
    std::string symbol;
    TradeSide side;
    double size;
    double entry_price;
    double exit_price;
    uint64_t entry_ts;
    uint64_t exit_ts;
    double gross_pnl;
    TradeCostBreakdown costs;
    double net_pnl;
};

class TradeLedger {
public:
    TradeLedger();
    uint64_t open_trade(const std::string& symbol, TradeSide side, double size, double entry_price, uint64_t ts);
    void close_trade(uint64_t trade_id, double exit_price, uint64_t ts);
    void apply_costs(uint64_t trade_id, double spread_cost, double commission_cost, double latency_cost);
    const std::vector<TradeLedgerEntry>& all_trades() const;
    double total_net_pnl() const;
    double total_gross_pnl() const;
private:
    uint64_t next_trade_id_;
    std::vector<TradeLedgerEntry> trades_;
    std::unordered_map<uint64_t, size_t> index_;
};
