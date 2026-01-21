#pragma once
#include <vector>
#include <string>

struct EngineRow {
    std::string symbol;
    std::string state;
    double net_bps;
    double dd_bps;
    int trades;
    double fees;
    double alloc;
    double leverage;
};

struct TradeRow {
    std::string engine;
    std::string symbol;
    std::string side;
    double bps;
    int latency_ms;
    double leverage;
};

// Legacy compatibility
using TelemetryEngineRow = EngineRow;
using TelemetryTradeRow  = TradeRow;

#include "governance/GovernanceController.hpp"
using chimera::GovernanceSnapshot;

class TelemetryBus {
public:
    void updateGovernance(const GovernanceSnapshot& g);
    GovernanceSnapshot snapshotGovernance() const;

    static TelemetryBus& instance();

    // New API
    void recordTrade(const TradeRow& row);
    void setEngines(const std::vector<EngineRow>& rows);

    // Legacy API (wrappers)
    void updateEngine(const TelemetryEngineRow& row);
    void addTrade(const TelemetryTradeRow& row);

    std::vector<TradeRow> snapshotTrades() const;
    std::vector<EngineRow> snapshotEngines() const;

private:
    GovernanceSnapshot governance_{};

    TelemetryBus() = default;

    std::vector<TradeRow> trades_;
    std::vector<EngineRow> engines_;
};
