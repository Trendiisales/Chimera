#include "binance/PrometheusServer.hpp"
#include <iostream>

namespace binance {

PrometheusServer::PrometheusServer(int p)
: port(p), running(false) {}

PrometheusServer::~PrometheusServer() {
    running.store(false, std::memory_order_release);
}

void PrometheusServer::start() {
    if (running.exchange(true))
        return;

    std::cout << "[METRICS] listening on :" << port << "\n";
}

}
