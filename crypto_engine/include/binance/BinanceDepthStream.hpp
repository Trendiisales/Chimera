#pragma once

#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

namespace binance {

class OrderBook;
class VenueHealth;

class BinanceDepthStream {
public:
    BinanceDepthStream(OrderBook& book, VenueHealth& health);
    ~BinanceDepthStream();

    void start();
    void stop();

private:
    void run();

    OrderBook& book_;
    VenueHealth& health_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace binance
