#pragma once

#include <thread>
#include <atomic>

namespace binance {

class PrometheusServer {
public:
    explicit PrometheusServer(int port);
    ~PrometheusServer();

    void start();
    void stop();

private:
    int port;
    std::atomic<bool> running{false};
    std::thread server_thread;

    void run();
};

} // namespace binance
