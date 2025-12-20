#pragma once

#include "BinanceRestClient.hpp"
#include "BinanceOrderBook.hpp"
#include "BinanceDeltaGate.hpp"
#include "BinanceHealth.hpp"
#include "BinaryLogWriter.hpp"
#include "TlsWebSocket.hpp"

#include <thread>
#include <atomic>
#include <string>

namespace binance {

class BinanceHFTFeed {
public:
    BinanceHFTFeed(
        const std::string& symbol,
        BinanceRestClient& rest,
        TlsWebSocket& ws,
        OrderBook& book,
        DeltaGate& gate,
        VenueHealth& health,
        BinaryLogWriter& blog
    );

    void start();
    void stop();

private:
    std::string symbol;
    BinanceRestClient& rest;
    TlsWebSocket& ws;
    OrderBook& book;
    DeltaGate& gate;
    VenueHealth& health;
    BinaryLogWriter& blog;

    std::atomic<bool> running{false};
    std::thread engine_thread;

    void engine_loop();
    void sleep_backoff(int& ms);
};

} // namespace binance
