#pragma once

#include "BinanceHFTFeed.hpp"
#include "BinaryLogWriter.hpp"

#include <memory>
#include <string>
#include <vector>

namespace binance {

class BinanceSupervisor {
public:
    BinanceSupervisor(
        BinanceRestClient& rest,
        const std::string& ws_host,
        int ws_port,
        const std::string& log_dir
    );

    void add_symbol(const std::string& symbol);
    void start_all();
    void stop_all();

private:
    struct FeedBundle {
        std::string symbol;

        std::unique_ptr<TlsWebSocket> ws;
        std::unique_ptr<OrderBook> book;
        std::unique_ptr<DeltaGate> gate;
        std::unique_ptr<VenueHealth> health;
        std::unique_ptr<BinaryLogWriter> blog;
        std::unique_ptr<BinanceHFTFeed> feed;
    };

    BinanceRestClient& rest;
    std::string ws_host;
    int ws_port;
    std::string log_dir;

    std::vector<FeedBundle> feeds;
};

} // namespace binance
