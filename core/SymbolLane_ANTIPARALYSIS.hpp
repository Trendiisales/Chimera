#pragma once

#include <string>
#include "../tier3/TickData.hpp"
#include "../telemetry/TelemetryBus.hpp"

class SymbolLane {
public:
    explicit SymbolLane(const std::string& sym);
    void onTick(const tier3::TickData& t);

private:
    std::string symbol_;

    double net_bps_;
    double dd_bps_;
    int    trade_count_;
    double fees_paid_;
    double alloc_;
    double leverage_;
};
