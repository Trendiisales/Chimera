#pragma once
#include <string>
#include <vector>
#include <memory>

#include "BinanceRestClient.hpp"
#include "OrderBook.hpp"
#include "DeltaGate.hpp"
#include "VenueHealth.hpp"
#include "BinaryLogWriter.hpp"
#include "BinanceDepthFeed.hpp"

namespace binance {

class BinanceSupervisor {
public:
    BinanceSupervisor(
        BinanceRestClient& rest,
        const std::string& log_dir,
        int metrics_port,
        const std::string& venue
    );

    ~BinanceSupervisor();

    void add_symbol(const std::string& symbol);
    void start_all();

private:
    struct FeedBundle {
        std::unique_ptr<OrderBook> book;
        std::unique_ptr<DeltaGate> gate;
        std::unique_ptr<VenueHealth> health;
        std::unique_ptr<BinaryLogWriter> blog;
        std::unique_ptr<BinanceDepthFeed> feed;
    };

    BinanceRestClient& rest;
    std::string log_dir;
    std::vector<FeedBundle> feeds;
};

}
