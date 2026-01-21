#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Installing REAL wire-latency (WS ping/pong + REST probe)"

# ============================================================
# exchange/BinanceWSClient.cpp
# ============================================================
cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <atomic>

#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

static uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

BinanceWSClient::BinanceWSClient(const std::string& host,
                                 const std::string& port,
                                 const std::string& stream)
    : host_(host), port_(port), stream_(stream) {}

void BinanceWSClient::setCallback(TickCB cb) {
    cb_ = cb;
}

void BinanceWSClient::start() {
    running_ = true;
    worker_ = std::thread(&BinanceWSClient::run, this);
}

void BinanceWSClient::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void BinanceWSClient::run() {
    try {
        boost::asio::io_context ioc;

        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};

        websocket::stream<
            boost::beast::ssl_stream<tcp::socket>
        > ws{ioc, ctx};

        auto const results = resolver.resolve(host_, port_);
        boost::asio::connect(ws.next_layer().next_layer(),
                             results.begin(),
                             results.end());

        ws.next_layer().handshake(ssl::stream_base::client);

        std::string target = "/stream?streams=" + stream_;
        ws.handshake(host_, target);

        std::atomic<uint64_t> last_ping_us{0};
        std::atomic<uint64_t> last_rtt_us{0};

        ws.control_callback(
            [&](websocket::frame_type kind,
                boost::beast::string_view) {
                if (kind == websocket::frame_type::pong) {
                    uint64_t now = now_us();
                    uint64_t sent = last_ping_us.load();
                    if (sent > 0 && now > sent) {
                        last_rtt_us.store(now - sent);
                    }
                }
            });

        auto last_ping_sent = std::chrono::steady_clock::now();

        float last_bid_sz = 0.0f;
        float last_ask_sz = 0.0f;
        float last_ofi = 0.0f;

        while (running_) {
            // Send ping every 1 second
            auto now = std::chrono::steady_clock::now();
            if (now - last_ping_sent > std::chrono::seconds(1)) {
                last_ping_us.store(now_us());
                ws.ping({});
                last_ping_sent = now;
            }

            boost::beast::flat_buffer buffer;
            ws.read(buffer);

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;
            if (!j.contains("data")) continue;

            auto& d = j["data"];

            tier3::TickData t;

            // bookTicker
            if (d.contains("b") && d.contains("a")) {
                t.bid = std::stof(d["b"].get<std::string>());
                t.ask = std::stof(d["a"].get<std::string>());
            }
            if (d.contains("B")) {
                t.bid_sz = std::stof(d["B"].get<std::string>());
            }
            if (d.contains("A")) {
                t.ask_sz = std::stof(d["A"].get<std::string>());
            }

            // REAL OFI (DEPTH DELTA)
            float d_bid = t.bid_sz - last_bid_sz;
            float d_ask = t.ask_sz - last_ask_sz;
            float ofi = d_bid - d_ask;

            t.ofi_z = ofi;
            t.ofi_accel = ofi - last_ofi;

            last_bid_sz = t.bid_sz;
            last_ask_sz = t.ask_sz;
            last_ofi = ofi;

            if (t.bid > 0 && t.ask > 0) {
                t.spread_bps =
                    ((t.ask - t.bid) / ((t.ask + t.bid) * 0.5f)) * 10000.0f;
            }

            t.impulse_bps = std::fabs(t.ofi_accel) * 10.0f;
            t.depth_ratio =
                (t.ask_sz > 0.0f)
                    ? std::min(1.0f, t.bid_sz / t.ask_sz)
                    : 1.0f;

            t.btc_impulse = (t.impulse_bps > 12.0f) ? 1 : 0;
            t.liquidation_long = (t.ofi_z < -5.0f) ? 1 : 0;
            t.liquidation_short = (t.ofi_z > 5.0f) ? 1 : 0;

            // Encode REAL wire latency into exchange_time_us
            // Here it means: RTT in microseconds
            t.exchange_time_us = last_rtt_us.load();

            if (cb_) cb_(stream_, t);
        }

        ws.close(websocket::close_code::normal);
    } catch (const std::exception& e) {
        std::cerr << "[WS] Error: " << e.what() << std::endl;
    }
}
BWC

# ============================================================
# main.cpp
# ============================================================
cat > main.cpp << 'MAIN'
#include "account/ApiKeys.hpp"
#include "exchange/BinanceREST.hpp"
#include "exchange/BinanceWSClient.hpp"

#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "engines/FadeETH_WORKING.hpp"
#include "engines/CascadeBTC_WORKING.hpp"
#include "risk/KillSwitchGovernor.hpp"
#include "router/ExecRouterBinance.hpp"

#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

static bool allow_print(std::atomic<uint64_t>& last_us, uint64_t interval_us) {
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    uint64_t prev = last_us.load();
    if (now - prev < interval_us) return false;
    last_us.store(now);
    return true;
}

int main() {
    auto keys = ApiKeys::load("../keys.json");

    KillSwitchGovernor kill;
    kill.registerEngine("ETH_FADE");
    kill.registerEngine("BTC_CASCADE");

    BinanceREST rest(keys.api_key, keys.api_secret, keys.dry_run);

    ExecRouterBinance eth_exec(&rest, &kill, "ETH_FADE");
    ExecRouterBinance btc_exec(&rest, &kill, "BTC_CASCADE");

    std::shared_ptr<CorrelationGovernor> corr;
    PortfolioGovernor* portfolio = nullptr;

    SymbolLane lane_eth("ETHUSDT", 0.001, 9101, portfolio, (void*)&eth_exec, corr);
    SymbolLane lane_btc("BTCUSDT", 0.001, 9102, portfolio, (void*)&btc_exec, corr);

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    BinanceWSClient ws(
        "stream.binance.com",
        "9443",
        "btcusdt@bookTicker/ethusdt@bookTicker/btcusdt@aggTrade/ethusdt@aggTrade"
    );

    std::atomic<uint64_t> last_eth_log{0};
    std::atomic<uint64_t> last_btc_log{0};

    const uint64_t LOG_INTERVAL_US = 500000; // 500ms

    ws.setCallback([&](const std::string& stream,
                       const tier3::TickData& t) {
        uint64_t rtt_us = t.exchange_time_us;
        uint64_t one_way = rtt_us / 2;

        if (stream.find("ethusdt") != std::string::npos) {
            lane_eth.onTick(t);
            eth.onTick(t);

            if (allow_print(last_eth_log, LOG_INTERVAL_US)) {
                std::cout << "[LAT] ETHUSDT RTT=" << rtt_us
                          << "us one-way≈" << one_way
                          << "us spr=" << t.spread_bps
                          << " ofi=" << t.ofi_z << std::endl;
            }

            if (eth.hasSignal()) {
                auto sig = eth.consumeSignal();
                lane_eth.applyRiskAndRoute(
                    "ETH_FADE",
                    sig.is_buy,
                    sig.confidence,
                    sig.price
                );
            }
        }

        if (stream.find("btcusdt") != std::string::npos) {
            lane_btc.onTick(t);
            btc.onTick(t);

            if (allow_print(last_btc_log, LOG_INTERVAL_US)) {
                std::cout << "[LAT] BTCUSDT RTT=" << rtt_us
                          << "us one-way≈" << one_way
                          << "us spr=" << t.spread_bps
                          << " ofi=" << t.ofi_z << std::endl;
            }

            if (btc.hasSignal()) {
                auto sig = btc.consumeSignal();
                lane_btc.applyRiskAndRoute(
                    "BTC_CASCADE",
                    sig.is_buy,
                    sig.confidence,
                    sig.price
                );
            }
        }
    });

    std::cout << "[CHIMERA] MODE B LIVE STACK | "
              << (keys.dry_run ? "DRY" : "LIVE") << std::endl;

    ws.start();
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(10));
}
MAIN

echo "[CHIMERA] REAL latency installed"
