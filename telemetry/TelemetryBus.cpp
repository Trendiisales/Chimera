#include "TelemetryBus.hpp"

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

void TelemetryBus::updateEngine(const TelemetryEngineRow& row) {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& e : engines_) {
        if (e.symbol == row.symbol) {
            e = row;
            return;
        }
    }
    engines_.push_back(row);
}

void TelemetryBus::recordTrade(const TelemetryTradeRow& row) {
    std::lock_guard<std::mutex> g(mu_);
    trades_.push_back(row);
    if (trades_.size() > 200) {
        trades_.erase(trades_.begin());
    }
}

std::vector<TelemetryEngineRow> TelemetryBus::snapshotEngines() {
    std::lock_guard<std::mutex> g(mu_);
    return engines_;
}

std::vector<TelemetryTradeRow> TelemetryBus::snapshotTrades() {
    std::lock_guard<std::mutex> g(mu_);
    return trades_;
}
