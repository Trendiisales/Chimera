#pragma once
#include <string>
#include "../telemetry/TelemetryBus.hpp"
#include "include/chimera/execution/ExchangeIO.hpp"

class SymbolLane {
public:
    explicit SymbolLane(const std::string& sym);
    void tick();
    void onTick(const chimera::MarketTick& tick);

private:
    std::string symbol_;
    double net_bps_;
    double dd_bps_;
    int trade_count_;
    double fees_;
    double alloc_;
    double leverage_;
    double last_price_;
    double position_;
    double last_mid_;
    int ticks_since_trade_;
    int warmup_ticks_;  // NEW
};
