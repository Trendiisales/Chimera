#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <functional>

class TelemetryServer {
public:
    using SnapshotCallback = std::function<std::string()>;

    TelemetryServer(int port, SnapshotCallback cb);
    ~TelemetryServer();

    void start();
    void stop();

private:
    void run();
    std::string buildHttpResponse(const std::string& body);
    std::string dashboardHtml();

    int port_;
    SnapshotCallback snapshotCb_;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
};
