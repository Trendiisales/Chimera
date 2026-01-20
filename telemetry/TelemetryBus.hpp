#pragma once

#include <mutex>
#include <vector>
#include <string>

struct TelemetryEngineRow {
    std::string symbol;
    double net_bps;
    double dd_bps;
    int    trades;
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
    int latency_ms;
    double leverage;
};

class TelemetryBus {
public:
    static TelemetryBus& instance();

    void updateEngine(const TelemetryEngineRow& row);
    void recordTrade(const TelemetryTradeRow& row);

    std::vector<TelemetryEngineRow> snapshotEngines();
    std::vector<TelemetryTradeRow> snapshotTrades();

private:
    TelemetryBus() = default;

    std::mutex mu_;
    std::vector<TelemetryEngineRow> engines_;
    std::vector<TelemetryTradeRow> trades_;
};
