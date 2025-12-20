#include "binance/BinanceDepthAdapterStub.hpp"
#include <chrono>
#include <iostream>

namespace binance {

void BinanceDepthAdapterStub::start(const DepthCallback& cb) {
    running.store(true);

    th = std::thread([this, cb]() {
        uint64_t u = 1001;
        int count = 0;

        while (running.load()) {
            DepthDelta d;
            d.U = u;
            d.u = u;

            cb(d);

            std::cout << "[ADAPTER] delta u=" << u << std::endl;
            u++;

            count++;
            if (count == 20) {
                std::cout << "[ADAPTER] injecting gap" << std::endl;
                u += 5;
                count = 0;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void BinanceDepthAdapterStub::stop() {
    running.store(false);
    if (th.joinable())
        th.join();
}

}
