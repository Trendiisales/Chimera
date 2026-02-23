#include "TelemetryWsServer.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace chimera {
namespace telemetry {

TelemetryWsServer::TelemetryWsServer(SnapshotPublisher& publisher, int port)
    : m_publisher(publisher), m_port(port) {}

TelemetryWsServer::~TelemetryWsServer() {
    if (m_running.load())
        stop();
}

void TelemetryWsServer::start() {
    m_running.store(true);
    m_thread = std::thread(&TelemetryWsServer::server_loop, this);
}

void TelemetryWsServer::stop() {
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.join();
}

std::string TelemetryWsServer::snapshot_to_json(const DeskSnapshot& s) {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    
    json << "{"
         << "\"global_exposure\":" << s.global_exposure << ","
         << "\"hft_exposure\":" << s.hft_exposure << ","
         << "\"structure_exposure\":" << s.structure_exposure << ","
         << "\"daily_pnl\":" << s.daily_pnl << ","
         << "\"latency_ema\":" << s.latency_ema << ","
         << "\"slippage_ema\":" << s.slippage_ema << ","
         << "\"hft_score\":" << s.hft_score << ","
         << "\"structure_score\":" << s.structure_score << ","
         << "\"hft_threshold\":" << s.hft_threshold << ","
         << "\"structure_threshold\":" << s.structure_threshold << ","
         << "\"spread_limit\":" << s.spread_limit << ","
         << "\"vol_limit\":" << s.vol_limit << ","
         << "\"lockdown_mode\":" << (s.lockdown_mode ? "true" : "false") << ","
         << "\"total_trades\":" << s.total_trades << ","
         << "\"timestamp\":" << s.timestamp_ns
         << "}";
    
    return json.str();
}

void TelemetryWsServer::server_loop() {
    // Simplified polling loop - in production use proper WebSocket library
    while (m_running.load()) {
        DeskSnapshot snapshot = m_publisher.read();
        std::string json = snapshot_to_json(snapshot);
        
        // In production: send via WebSocket
        // For now, this is the polling mechanism
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace telemetry
} // namespace chimera
