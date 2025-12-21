#pragma once

#include <string>
#include <thread>
#include <atomic>

class MetricsHttpServer {
public:
    explicit MetricsHttpServer(int port);
    ~MetricsHttpServer();

    void start();
    void stop();

private:
    void run();
    std::string read_metrics();

    int port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
