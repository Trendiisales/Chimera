#include "binance/BinanceHFTFeed.hpp"
#include "binance/BinanceDepthParser.hpp"
#include "binance/PrometheusMetrics.hpp"

#include <chrono>
#include <algorithm>

namespace binance {

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

BinanceHFTFeed::BinanceHFTFeed(
    const std::string& symbol_,
    BinanceRestClient& rest_,
    TlsWebSocket& ws_,
    OrderBook& book_,
    DeltaGate& gate_,
    VenueHealth& health_,
    BinaryLogWriter& blog_)
    : symbol(symbol_),
      rest(rest_),
      ws(ws_),
      book(book_),
      gate(gate_),
      health(health_),
      blog(blog_) {}

void BinanceHFTFeed::start() {
    if (running)
        return;
    running = true;
    engine_thread = std::thread(&BinanceHFTFeed::engine_loop, this);
}

void BinanceHFTFeed::stop() {
    running = false;
    ws.stop();
    if (engine_thread.joinable())
        engine_thread.join();
}

void BinanceHFTFeed::sleep_backoff(int& ms) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(ms));
}

void BinanceHFTFeed::engine_loop() {
    auto& metrics =
        PrometheusMetrics::instance().for_symbol(symbol);

    int backoff_ms = 500;

    while (running) {
        health.set(Health::YELLOW);
        metrics.health.store(2);

        auto snapshot = rest.fetch_depth_snapshot(symbol, 1000);
        if (snapshot.lastUpdateId == 0) {
            health.set(Health::RED);
            metrics.health.store(1);
            sleep_backoff(backoff_ms);
            continue;
        }

        metrics.snapshots.fetch_add(1);

        book.load_snapshot(snapshot);
        gate.reset(snapshot.lastUpdateId);

        blog.write_snapshot(
            &snapshot, sizeof(snapshot), now_ns());

        if (!ws.connect()) {
            metrics.reconnects.fetch_add(1);
            health.set(Health::RED);
            metrics.health.store(1);
            sleep_backoff(backoff_ms);
            continue;
        }

        health.set(Health::GREEN);
        metrics.health.store(3);
        backoff_ms = 500;

        ws.set_on_message([this, &metrics](const std::string& raw) {
            auto delta = BinanceDepthParser::parse(raw);
            if (!delta)
                return;

            auto r = gate.evaluate(*delta);
            if (r == DeltaResult::DROP_OLD)
                return;

            if (r == DeltaResult::GAP) {
                metrics.gaps.fetch_add(1);
                health.set(Health::RED);
                metrics.health.store(1);
                ws.stop();
                return;
            }

            metrics.deltas.fetch_add(1);

            blog.write_depth_delta(
                delta->U, delta->u,
                raw.data(), raw.size(),
                now_ns());

            book.apply_delta(*delta);
        });

        ws.run();

        health.set(Health::RED);
        metrics.health.store(1);
        sleep_backoff(backoff_ms);
        backoff_ms = std::min(backoff_ms * 2, 8000);
    }
}

}
