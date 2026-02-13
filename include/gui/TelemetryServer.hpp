#pragma once

#include <functional>
#include <string>
#include <thread>
#include <atomic>

class TelemetryServer {
public:
    using SnapshotFn = std::function<std::string()>;

    TelemetryServer(int port, SnapshotFn cb);
    ~TelemetryServer();

    void start();
    void stop();

private:
    void run();
    void handle_client(int client_fd);

    int port_;
    SnapshotFn snapshot_callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
