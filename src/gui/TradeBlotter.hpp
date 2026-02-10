#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct TradeRow {
    uint64_t id;
    std::string symbol;
    double qty;
    double entry_px;
    double exit_px;
    double fees;
    double pnl;
    uint64_t ts;
};

class TradeBlotter {
public:
    void addEntry(uint64_t id, const std::string& sym, double qty, double px);
    void addExit(uint64_t id, double px, double fees);
    std::vector<TradeRow> snapshot() const;

private:
    std::vector<TradeRow> rows_;
};

extern TradeBlotter g_blotter;
