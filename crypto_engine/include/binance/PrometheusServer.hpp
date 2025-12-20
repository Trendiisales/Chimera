#pragma once
#include <atomic>

namespace binance {

class PrometheusServer {
public:
    explicit PrometheusServer(int port);
    ~PrometheusServer();

    void start();

private:
    int port;
    std::atomic<bool> running;
};

}
