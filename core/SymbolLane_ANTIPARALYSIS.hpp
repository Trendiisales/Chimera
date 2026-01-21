#pragma once
#include <string>
#include "../telemetry/TelemetryBus.hpp"

class SymbolLane {
public:
    explicit SymbolLane(const std::string& sym);
    void tick();

private:
    std::string symbol_;
    double net_bps_;
    double dd_bps_;
    int trade_count_;
    double fees_;
    double alloc_;
    double leverage_;
};
