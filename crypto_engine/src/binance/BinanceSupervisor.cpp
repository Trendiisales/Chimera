#include "binance/BinanceSupervisor.hpp"
#include <iostream>

namespace binance {

BinanceSupervisor::BinanceSupervisor(
    BinanceRestClient& r,
    const std::string& dir,
    int,
    const std::string&
)
: rest(r), log_dir(dir) {}

BinanceSupervisor::~BinanceSupervisor() = default;

void BinanceSupervisor::add_symbol(const std::string& symbol) {
    FeedBundle b;
    b.book   = std::make_unique<OrderBook>();
    b.gate   = std::make_unique<DeltaGate>();
    b.health = std::make_unique<VenueHealth>();
    b.blog   = std::make_unique<BinaryLogWriter>(log_dir + "/" + symbol + ".blog");
    b.feed   = std::make_unique<BinanceDepthFeed>(
        rest, *b.book, *b.gate, *b.health, *b.blog
    );

    feeds.push_back(std::move(b));
    std::cout << "[SUPERVISOR] Added symbol " << symbol << "\n";
}

void BinanceSupervisor::start_all() {
    for (auto& f : feeds) {
        f.feed->start();
    }
}

}
