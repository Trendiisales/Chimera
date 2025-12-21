#include "binance/BinanceSupervisor.hpp"

#include <iostream>
#include <utility>

namespace binance {

BinanceSupervisor::BinanceSupervisor(
    BinanceRestClient& rest,
    const std::string& log_dir,
    int metrics_port,
    const std::string& venue
)
: rest_(rest),
  log_dir_(log_dir),
  metrics_port_(metrics_port),
  venue_(venue)
{}

void BinanceSupervisor::add_symbol(const std::string& symbol) {
    // Order book
    books_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbol),
        std::forward_as_tuple()
    );

    // Delta gate
    gates_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbol),
        std::forward_as_tuple()
    );

    // Venue health (non-copyable atomics)
    health_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbol),
        std::forward_as_tuple()
    );

    // Binary log writer (by value, as per header)
    logs_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbol),
        std::forward_as_tuple(log_dir_ + "/" + symbol + ".blog")
    );

    // Depth feed (by value, snapshot-only)
    feeds_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(symbol),
        std::forward_as_tuple(
            rest_,
            books_.at(symbol),
            gates_.at(symbol),
            health_.at(symbol),
            logs_.at(symbol)
        )
    );

    std::cout << "[SUPERVISOR] Added symbol " << symbol << "\n";
}

void BinanceSupervisor::start() {
    for (auto& kv : feeds_) {
        kv.second.start();
    }
}

const std::unordered_map<std::string, OrderBook>&
BinanceSupervisor::books() const {
    return books_;
}

} // namespace binance
