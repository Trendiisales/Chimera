#include "ShadowExecutor.hpp"
#include "telemetry/TelemetryBus.hpp"

void ShadowExecutor::onIntent(
    const std::string& engine,
    const std::string& symbol,
    double bps,
    double latency_ms
) {
    TelemetryTradeRow row;
    row.engine = engine;
    row.symbol = symbol;
    row.side = "BUY";
    row.bps = bps;
    row.latency_ms = latency_ms;
    row.leverage = 1.0;

    TelemetryBus::instance().recordTrade(row);
}
