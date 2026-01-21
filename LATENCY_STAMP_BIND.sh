#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Binding exchange_time_us + live latency print"

# ============================================================
# exchange/BinanceWSClient.cpp
# ============================================================
cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <cmath>

#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
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

        float last_ofi = 0.0f;

        while (running_) {
            boost::beast::flat_buffer buffer;
            ws.read(buffer);

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;

            if (!j.contains("data")) continue;
            auto& d = j["data"];

            tier3::TickData t;
            t.ts_ns = now_ns();

            // Exchange event time (Binance uses ms)
            // bookTicker: "E", aggTrade: "T"
            if (d.contains("E")) {
                t.exchange_time_us =
                    static_cast<uint64_t>(d["E"].get<uint64_t>()) * 1000ULL;
            } else if (d.contains("T")) {
                t.exchange_time_us =
                    static_cast<uint64_t>(d["T"].get<uint64_t>()) * 1000ULL;
            } else {
                t.exchange_time_us = now_us();
            }

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

            // aggTrade quantity → OFI proxy
            float trade_qty = 0.0f;
            if (d.contains("q")) {
                trade_qty = std::stof(d["q"].get<std::string>());
            }

            t.ofi_z = trade_qty;
            t.ofi_accel = t.ofi_z - last_ofi;
            last_ofi = t.ofi_z;

            if (t.bid > 0 && t.ask > 0) {
                t.spread_bps =
                    ((t.ask - t.bid) / ((t.ask + t.bid) * 0.5f)) * 10000.0f;
            }

            t.impulse_bps = std::fabs(t.ofi_accel) * 10.0f;

            t.depth_ratio =
                (t.ask_sz > 0.0f)
                    ? std::min(1.0f, t.bid_sz / t.ask_sz)
                    : 1.0f;

            t.btc_impulse = (t.impulse_bps > 10.0f) ? 1 : 0;
            t.liquidation_long = (t.ofi_z < -5.0f) ? 1 : 0;
            t.liquidation_short = (t.ofi_z > 5.0f) ? 1 : 0;

            if (cb_) cb_(stream_, t);
        }

        ws.close(websocket::close_code::normal);
    } catch (const std::exception& e) {
        std::cerr << "[WS] Error: " << e.what() << std::endl;
    }
}
BWC

# ============================================================
# main.cpp (latency print, non-invasive)
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

static uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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

    SymbolLane lane_eth(
        "ETHUSDT",
        0.001,
        9101,
        portfolio,
        (void*)&eth_exec,
        corr
    );

    SymbolLane lane_btc(
        "BTCUSDT",
        0.001,
        9102,
        portfolio,
        (void*)&btc_exec,
        corr
    );

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    BinanceWSClient ws(
        "stream.binance.com",
        "9443",
        "btcusdt@bookTicker/ethusdt@bookTicker/btcusdt@aggTrade/ethusdt@aggTrade"
    );

    ws.setCallback([&](const std::string& stream,
                       const tier3::TickData& t) {
        uint64_t lat_us = 0;
        if (t.exchange_time_us > 0) {
            uint64_t nowu = now_us();
            lat_us = (nowu > t.exchange_time_us)
                         ? (nowu - t.exchange_time_us)
                         : 0;
        }

        if (stream.find("ethusdt") != std::string::npos) {
            std::cout << "[LAT] ETHUSDT " << lat_us << "us "
                      << "spr=" << t.spread_bps
                      << " ofi=" << t.ofi_z << std::endl;

            lane_eth.onTick(t);
            eth.onTick(t);

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
            std::cout << "[LAT] BTCUSDT " << lat_us << "us "
                      << "spr=" << t.spread_bps
                      << " ofi=" << t.ofi_z << std::endl;

            lane_btc.onTick(t);
            btc.onTick(t);

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

echo "[CHIMERA] Latency binding installed"
