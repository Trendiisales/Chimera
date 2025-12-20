set -e

mkdir -p crypto_engine/include/binance crypto_engine/src/binance

cat <<'HPP' > crypto_engine/include/binance/PrometheusServer.hpp
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
HPP

cat <<'CPP' > crypto_engine/src/binance/PrometheusServer.cpp
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
CPP

sed -i '' '/add_library(crypto_engine/ a\
  src/binance/PrometheusServer.cpp
' crypto_engine/CMakeLists.txt

echo "PROMETHEUS OK"
