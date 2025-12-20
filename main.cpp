#include "binance/BinanceSupervisor.hpp"
#include "binance/PrometheusServer.hpp"

#include <thread>
#include <chrono>

using namespace binance;

int main() {
    BinanceRestClient rest;

    PrometheusServer metrics(9102);
    metrics.start();

    BinanceSupervisor supervisor(
        rest,
        "stream.binance.com",
        9443,
        "./logs"
    );

    supervisor.add_symbol("BTCUSDT");
    supervisor.add_symbol("ETHUSDT");

    supervisor.start_all();

    while (true) {
        std::this_thread::sleep_for(
            std::chrono::seconds(30));
    }
}
