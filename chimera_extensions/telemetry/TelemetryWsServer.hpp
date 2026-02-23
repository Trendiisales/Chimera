#pragma once

#include "SnapshotPublisher.hpp"
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

namespace chimera {
namespace telemetry {

// Minimal WebSocket server for Chrome GUI
class TelemetryWsServer {
public:
    explicit TelemetryWsServer(SnapshotPublisher& publisher, int port = 9000);
    ~TelemetryWsServer();

    void start();
    void stop();

private:
    void server_loop();
    std::string snapshot_to_json(const DeskSnapshot& snapshot);

    SnapshotPublisher& m_publisher;
    int m_port;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace telemetry
} // namespace chimera
