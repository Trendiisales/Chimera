#include "ShadowExecutor.hpp"
#include "../telemetry/TelemetryBus.hpp"
#include <chrono>

void ShadowExecutor::onIntent(
    const std::string& engine,
    const std::string& symbol,
    double bps,
    double leverage
) {
    TelemetryTradeRow row;
    row.engine = engine;
    row.symbol = symbol;
    row.side = "BUY";
    row.bps = bps;
    row.latency_ms = 25;
    row.leverage = leverage;

    TelemetryBus::instance().recordTrade(row);
}
