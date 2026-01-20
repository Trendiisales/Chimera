#pragma once
#include <thread>
#include <atomic>

class TelemetryServer {
public:
    TelemetryServer(int port);
    void start();
    void stop();
private:
    void run();
    int port_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
