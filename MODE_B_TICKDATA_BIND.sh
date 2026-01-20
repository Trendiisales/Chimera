#!/usr/bin/env bash
set -e

echo "[CHIMERA] Binding WS → tier3::TickData (REAL CONTRACT)"

# ============================================================
# exchange/BinanceWSClient.hpp
# ============================================================
cat > exchange/BinanceWSClient.hpp << 'BWH'
#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>

#include "../tier3/TickData.hpp"

class BinanceWSClient {
public:
    using TickCB = std::function<void(const std::string& stream,
                                      const tier3::TickData&)>;

    BinanceWSClient(const std::string& host,
                    const std::string& port,
                    const std::string& stream);

    void setCallback(TickCB cb);
    void start();
    void stop();

private:
    void run();

    std::string host_;
    std::string port_;
    std::string stream_;

    TickCB cb_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
BWH

# ============================================================
# exchange/BinanceWSClient.cpp
# ============================================================
cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <cmath>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
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
        tcp::resolver resolver(ioc);
        websocket::stream<tcp::socket> ws(ioc);

        auto const results = resolver.resolve(host_, port_);
        boost::asio::connect(ws.next_layer(), results.begin(), results.end());

        std::string target = "/ws/" + stream_;
        ws.handshake(host_, target);

        float last_ofi = 0.0f;

        while (running_) {
            boost::beast::flat_buffer buffer;
            ws.read(buffer);

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;

            tier3::TickData t;
            t.ts_ns = now_ns();

            // bookTicker
            if (j.contains("b") && j.contains("a")) {
                t.bid = std::stof(j["b"].get<std::string>());
                t.ask = std::stof(j["a"].get<std::string>());
            }
            if (j.contains("B")) {
                t.bid_sz = std::stof(j["B"].get<std::string>());
            }
            if (j.contains("A")) {
                t.ask_sz = std::stof(j["A"].get<std::string>());
            }

            // aggTrade
            float trade_qty = 0.0f;
            if (j.contains("q")) {
                trade_qty = std::stof(j["q"].get<std::string>());
            }

            // OFI proxy + accel
            t.ofi_z = trade_qty;
            t.ofi_accel = t.ofi_z - last_ofi;
            last_ofi = t.ofi_z;

            // Spread
            if (t.bid > 0 && t.ask > 0) {
                t.spread_bps =
                    ((t.ask - t.bid) / ((t.ask + t.bid) * 0.5f)) * 10000.0f;
            }

            // Impulse
            t.impulse_bps = std::fabs(t.ofi_accel) * 10.0f;

            // Heuristics
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
        // Route by stream name instead of TickData field
        if (stream.find("ethusdt") != std::string::npos) {
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
    while (true) std::this_thread::sleep_for(std::chrono::seconds(10));
}
MAIN

echo "[CHIMERA] TickData contract bound"
