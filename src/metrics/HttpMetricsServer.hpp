#pragma once
#include <atomic>
#include <thread>

class HttpMetricsServer {
public:
    explicit HttpMetricsServer(int port);
    ~HttpMetricsServer();

    void start();
    void stop();

    void inc_intents();
private:
    int port_;
    int server_fd_;
    std::atomic<unsigned long long> intents_;
    std::atomic<bool> running_;
    std::thread worker_;
};
