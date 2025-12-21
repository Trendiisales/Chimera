#include "binance/BinanceDepthStream.hpp"
#include "binance/OrderBook.hpp"
#include "binance/VenueHealth.hpp"
#include "latency/LatencyRegistry.hpp"

#include <chrono>
#include <thread>

namespace binance {

using clock_type = std::chrono::steady_clock;

BinanceDepthStream::BinanceDepthStream(
    OrderBook& book,
    VenueHealth& health
)
: book_(book), health_(health) {}

BinanceDepthStream::~BinanceDepthStream() {
    stop();
}

void BinanceDepthStream::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&BinanceDepthStream::run, this);
}

void BinanceDepthStream::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
}

void BinanceDepthStream::run() {
    while (running_.load(std::memory_order_relaxed)) {
        auto t0 = clock_type::now();

        // Streaming heartbeat (no OrderBook mutation yet)
        health_.set(Health::GREEN);

        auto t1 = clock_type::now();
        g_latency.feed_to_book_ns.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                t1 - t0
            ).count(),
            std::memory_order_relaxed
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace binance
