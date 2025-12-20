set -e

mkdir -p crypto_engine/include/binance crypto_engine/src/binance

cat <<'HPP' > crypto_engine/include/binance/BinanceSupervisor.hpp
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
HPP

cat <<'CPP' > crypto_engine/src/binance/BinanceSupervisor.cpp
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
CPP

sed -i '' '/add_library(crypto_engine/ a\
  src/binance/BinanceSupervisor.cpp
' crypto_engine/CMakeLists.txt

echo "SUPERVISOR OK"
