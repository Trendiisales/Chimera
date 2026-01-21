#include "SymbolLane_ANTIPARALYSIS.hpp"

SymbolLane::SymbolLane(const std::string& sym)
    : symbol_(sym),
      net_bps_(0.0),
      dd_bps_(0.0),
      trade_count_(0),
      fees_(0.0),
      alloc_(1.0),
      leverage_(1.0) {}

void SymbolLane::tick() {
    TelemetryEngineRow row;
    row.symbol = symbol_;
    row.net_bps = net_bps_;
    row.dd_bps = dd_bps_;
    row.trades = trade_count_;
    row.fees = fees_;
    row.alloc = alloc_;
    row.leverage = leverage_;
    row.state = "LIVE";

    TelemetryBus::instance().updateEngine(row);
}
