#include "BinanceWSClient.hpp"

#include <iostream>
#include <chrono>
#include <thread>

using steady_clock = std::chrono::steady_clock;

BinanceWSClient::BinanceWSClient(const std::string& host,
                                 const std::string& port,
                                 const std::string& stream)
    : host_(host),
      port_(port),
      stream_(stream) {
}

void BinanceWSClient::setCallback(TickCB cb) {
    cb_ = cb;
}

void BinanceWSClient::start() {
    if (running_.load()) return;
    running_.store(true);
    worker_ = std::thread(&BinanceWSClient::run, this);
}

void BinanceWSClient::stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void BinanceWSClient::run() {
    static steady_clock::time_point last_print = steady_clock::now();
    uint64_t ticks = 0;

    // ---- SHADOW / SIM MODE FEED ----
    // This preserves full pipeline behavior without network noise.
    while (running_.load()) {
        if (cb_) {
            tier3::TickData t;
            t.bid = 100.0f;
            t.ask = 100.01f;
            t.spread_bps = 0.01f;
            t.ofi_z = 0.0f;

            cb_(stream_, t);
            ++ticks;
        }

        auto now = steady_clock::now();
        if (now - last_print >= std::chrono::minutes(1)) {
            last_print = now;
            std::cout << "[WS] Heartbeat | ticks/min=" << ticks
                      << " stream=" << stream_ << "\n";
            ticks = 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
