#include "TelemetryServer.hpp"
#include "TelemetryBus.hpp"

static void writeGovernance(std::ostream& body) {
    GovernanceSnapshot s = TelemetryBus::instance().snapshotGovernance();
    body << "\"governance\":{"
         << "\"regime_quality\":" << s.regime_quality << ","
         << "\"ladder_tier\":" << s.ladder_tier << ","
         << "\"eth_locked\":" << (s.eth_locked ? "true" : "false") << ","
         << "\"kill_enabled\":" << (s.kill_enabled ? "true" : "false")
         << "}";
}

void TelemetryServer::handleRequest(std::ostream& body) {
    auto engines = TelemetryBus::instance().snapshotEngines();
    auto trades  = TelemetryBus::instance().snapshotTrades();

    body << "{";
    writeGovernance(body);
    body << ",\"engines\":[";

    for (size_t i = 0; i < engines.size(); ++i) {
        const auto& e = engines[i];
        body << "{"
             << "\"symbol\":\"" << e.symbol << "\","
             << "\"net_bps\":" << e.net_bps << ","
             << "\"dd_bps\":" << e.dd_bps << ","
             << "\"trades\":" << e.trades << ","
             << "\"fees\":" << e.fees << ","
             << "\"alloc\":" << e.alloc << ","
             << "\"leverage\":" << e.leverage << ","
             << "\"state\":\"" << e.state << "\""
             << "}";
        if (i + 1 < engines.size()) body << ",";
    }

    body << "],\"trades\":[";

    for (size_t i = 0; i < trades.size(); ++i) {
        const auto& t = trades[i];
        body << "{"
             << "\"engine\":\"" << t.engine << "\","
             << "\"symbol\":\"" << t.symbol << "\","
             << "\"side\":\"" << t.side << "\","
             << "\"bps\":" << t.bps << ","
             << "\"latency_ms\":" << t.latency_ms << ","
             << "\"leverage\":" << t.leverage
             << "}";
        if (i + 1 < trades.size()) body << ",";
    }

    body << "]}";
}

#include <thread>
#include <chrono>
#include <iostream>

void runTelemetryServer(int port) {
    std::cout << "[TELEMETRY] Server started on port " << port << std::endl;
    
    // Simple telemetry server - just logs for now
    // In production this would be an HTTP server using httplib or similar
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        // Server is running but not serving HTTP (would need httplib)
        // Just keeping thread alive
    }
}
