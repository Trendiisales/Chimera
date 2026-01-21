#include "telemetry/TelemetryBus.hpp"
#include <mutex>

static std::mutex BUS_LOCK;

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

// New API
void TelemetryBus::recordTrade(const TradeRow& row) {
    std::lock_guard<std::mutex> g(BUS_LOCK);
    trades_.push_back(row);
    if (trades_.size() > 200) trades_.erase(trades_.begin());
}

void TelemetryBus::setEngines(const std::vector<EngineRow>& rows) {
    std::lock_guard<std::mutex> g(BUS_LOCK);
    engines_ = rows;
}

// Legacy API wrappers
void TelemetryBus::updateEngine(const TelemetryEngineRow& row) {
    std::lock_guard<std::mutex> g(BUS_LOCK);
    bool found = false;
    for (auto& e : engines_) {
        if (e.symbol == row.symbol) {
            e = row;
            found = true;
            break;
        }
    }
    if (!found) engines_.push_back(row);
}

void TelemetryBus::addTrade(const TelemetryTradeRow& row) {
    recordTrade(row);
}

std::vector<TradeRow> TelemetryBus::snapshotTrades() const {
    std::lock_guard<std::mutex> g(BUS_LOCK);
    return trades_;
}

std::vector<EngineRow> TelemetryBus::snapshotEngines() const {
    std::lock_guard<std::mutex> g(BUS_LOCK);
    return engines_;
}


void TelemetryBus::updateGovernance(const GovernanceSnapshot& g) {
    governance_ = g;
}

GovernanceSnapshot TelemetryBus::snapshotGovernance() const {
    return governance_;
}
