#include "gui_snapshot_bus.hpp"
#include "../../telemetry/TelemetryBus.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>

GuiSnapshotBus& GuiSnapshotBus::instance() {
    static GuiSnapshotBus bus;
    return bus;
}

std::string GuiSnapshotBus::get() {
    auto engines = TelemetryBus::instance().snapshotEngines();
    auto trades = TelemetryBus::instance().snapshotTrades();
    auto gov = TelemetryBus::instance().snapshotGovernance();
    
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    json << "{";
    json << "\"ts\":" << ms << ",";
    json << "\"governance\":{";
    json << "\"regime_quality\":" << gov.regime_quality << ",";
    json << "\"ladder_tier\":" << gov.ladder_tier << ",";
    json << "\"eth_locked\":" << (gov.eth_locked ? "true" : "false") << ",";
    json << "\"kill_enabled\":" << (gov.kill_enabled ? "true" : "false");
    json << "},";
    
    json << "\"engines\":[";
    for (size_t i = 0; i < engines.size(); ++i) {
        if (i > 0) json << ",";
        const auto& e = engines[i];
        json << "{";
        json << "\"symbol\":\"" << e.symbol << "\",";
        json << "\"state\":\"" << e.state << "\",";
        json << "\"net_bps\":" << e.net_bps << ",";
        json << "\"dd_bps\":" << e.dd_bps << ",";
        json << "\"trades\":" << e.trades << ",";
        json << "\"fees\":" << e.fees << ",";
        json << "\"alloc\":" << e.alloc << ",";
        json << "\"leverage\":" << e.leverage;
        json << "}";
    }
    json << "],";
    
    json << "\"trades\":[";
    for (size_t i = 0; i < trades.size() && i < 50; ++i) {
        if (i > 0) json << ",";
        const auto& t = trades[i];
        json << "{";
        json << "\"engine\":\"" << t.engine << "\",";
        json << "\"symbol\":\"" << t.symbol << "\",";
        json << "\"side\":\"" << t.side << "\",";
        json << "\"bps\":" << t.bps << ",";
        json << "\"latency_ms\":" << t.latency_ms << ",";
        json << "\"leverage\":" << t.leverage;
        json << "}";
    }
    json << "]";
    json << "}";
    
    return json.str();
}
