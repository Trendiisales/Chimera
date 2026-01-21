#pragma once
#include <string>

struct TelemetryEngineRow {
    std::string symbol;
    double net_bps;
    double dd_bps;
    int trades;
    double fees;
    double alloc;
    double leverage;
    std::string state;
};

struct TelemetryTradeRow {
    std::string engine;
    std::string symbol;
    std::string side;
    double bps;
    double latency_ms;
    double leverage;
};
