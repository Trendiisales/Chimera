#pragma once

#include "binance/BinanceRestClient.hpp"
#include "binance/BinanceDepthFeed.hpp"
#include "binance/OrderBook.hpp"
#include "binance/DeltaGate.hpp"
#include "binance/VenueHealth.hpp"
#include "binance/BinaryLogWriter.hpp"

#include <string>
#include <unordered_map>

namespace binance {

class BinanceSupervisor {
public:
    BinanceSupervisor(
        BinanceRestClient& rest,
        const std::string& log_dir,
        int metrics_port,
        const std::string& venue
    );

    void add_symbol(const std::string& symbol);
    void start();

    const std::unordered_map<std::string, OrderBook>& books() const;

private:
    BinanceRestClient& rest_;
    std::string log_dir_;
    int metrics_port_;
    std::string venue_;

    std::unordered_map<std::string, OrderBook> books_;
    std::unordered_map<std::string, DeltaGate> gates_;
    std::unordered_map<std::string, VenueHealth> health_;
    std::unordered_map<std::string, BinaryLogWriter> logs_;
    std::unordered_map<std::string, BinanceDepthFeed> feeds_;
};

} // namespace binance
