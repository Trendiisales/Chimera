#include "binance/BinanceWSDelta.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>

namespace binance {

static std::atomic<bool> running{false};
static std::thread ws_thread;

void start_ws(const DeltaCallback& cb) {
    running.store(true);

    ws_thread = std::thread([cb]() {
        uint64_t U = 1001;
        uint64_t u = 1001;
        int count = 0;

        while (running.load()) {
            DepthDelta d;
            d.U = U;
            d.u = u;

            cb(d);

            U = u + 1;
            u = U;

            count++;

            if (count == 20) {
                std::cout << "[WS] Injecting GAP" << std::endl;
                U += 5;
                u = U;
                count = 0;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void stop_ws() {
    running.store(false);
    if (ws_thread.joinable())
        ws_thread.join();
}

}
