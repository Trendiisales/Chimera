#include "binance/BinanceSupervisor.hpp"

#include <iostream>
#include <algorithm>

namespace binance {

BinanceSupervisor::BinanceSupervisor(
    BinanceRestClient& rest_,
    const std::string& ws_host_,
    int ws_port_,
    const std::string& log_dir_)
    : rest(rest_),
      ws_host(ws_host_),
      ws_port(ws_port_),
      log_dir(log_dir_) {}

void BinanceSupervisor::add_symbol(const std::string& symbol) {
    FeedBundle bundle;
    bundle.symbol = symbol;

    std::string path = "/ws/" + symbol;
    std::transform(path.begin(), path.end(), path.begin(), ::tolower);
    path += "@depth@100ms";

    bundle.ws = std::make_unique<TlsWebSocket>(
        ws_host, ws_port, path);

    bundle.book = std::make_unique<OrderBook>();
    bundle.gate = std::make_unique<DeltaGate>();
    bundle.health = std::make_unique<VenueHealth>();

    std::string log_path =
        log_dir + "/" + symbol + ".blog";

    bundle.blog = std::make_unique<BinaryLogWriter>(
        log_path, symbol);

    bundle.feed = std::make_unique<BinanceHFTFeed>(
        symbol,
        rest,
        *bundle.ws,
        *bundle.book,
        *bundle.gate,
        *bundle.health,
        *bundle.blog
    );

    feeds.push_back(std::move(bundle));

    std::cout << "[SUPERVISOR] Added symbol " << symbol << "\n";
}

void BinanceSupervisor::start_all() {
    for (auto& f : feeds)
        f.feed->start();
}

void BinanceSupervisor::stop_all() {
    for (auto& f : feeds)
        f.feed->stop();
}

} // namespace binance
