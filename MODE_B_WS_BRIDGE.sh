#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] MODE B — WS + EXEC BRIDGE DROP"

mkdir -p exchange router build

# ============================================================
# router/ExecRouterBinance.hpp
# ============================================================
cat > router/ExecRouterBinance.hpp << 'EHB'
#pragma once
#include <string>
#include "../exchange/BinanceREST.hpp"
#include "../risk/KillSwitchGovernor.hpp"

class ExecRouterBinance {
public:
    ExecRouterBinance(BinanceREST* rest,
                      KillSwitchGovernor* kill,
                      const std::string& engine_name)
        : rest_(rest), kill_(kill), engine_(engine_name) {}

    void send(bool is_buy, double size, double price, const std::string& symbol) {
        if (!kill_->globalEnabled()) return;
        if (!kill_->isEngineEnabled(engine_)) return;

        double scaled = kill_->scaleSize(engine_, size);
        if (scaled <= 0.0) return;

        rest_->sendOrder(
            symbol,
            is_buy ? "BUY" : "SELL",
            scaled,
            price,
            true
        );

        kill_->recordSignal(engine_, 0);
    }

private:
    BinanceREST* rest_;
    KillSwitchGovernor* kill_;
    std::string engine_;
};
EHB

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
    using TickCB = std::function<void(const tier3::TickData&)>;

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

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

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

        while (running_) {
            boost::beast::flat_buffer buffer;
            ws.read(buffer);

            std::string msg = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;

            tier3::TickData t;

            if (j.contains("s")) {
                t.symbol = j["s"].get<std::string>();
            }

            // bookTicker
            if (j.contains("b") && j.contains("a")) {
                t.bid = std::stod(j["b"].get<std::string>());
                t.ask = std::stod(j["a"].get<std::string>());
            }

            // aggTrade impulse
            if (j.contains("p") && j.contains("q")) {
                double px = std::stod(j["p"].get<std::string>());
                double qty = std::stod(j["q"].get<std::string>());
                t.last_trade_price = px;
                t.last_trade_qty = qty;
                t.impulse_bps = qty > 0 ? 10.0f : 0.0f;
            }

            // Derivatives for engines
            if (t.ask > 0 && t.bid > 0) {
                t.spread_bps = ((t.ask - t.bid) / ((t.ask + t.bid) * 0.5)) * 10000.0;
                t.depth_ratio = 1.0f;
                t.ofi_z = (float)(t.last_trade_qty * 0.1);
            }

            if (cb_) cb_(t);
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

    ws.setCallback([&](const tier3::TickData& t) {
        if (t.symbol == "ETHUSDT") {
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

        if (t.symbol == "BTCUSDT") {
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

# ============================================================
# CMakeLists.txt
# ============================================================
cat > CMakeLists.txt << 'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
  ${ROOT}
  ${ROOT}/core
  ${ROOT}/engines
  ${ROOT}/risk
  ${ROOT}/tier3
  ${ROOT}/metrics
  ${ROOT}/exchange
  ${ROOT}/router
  ${ROOT}/account
)

add_executable(chimera
  ${ROOT}/main.cpp
  ${ROOT}/exchange/BinanceREST.cpp
  ${ROOT}/exchange/BinanceWSClient.cpp
)

target_link_libraries(chimera
  OpenSSL::SSL
  OpenSSL::Crypto
  CURL::libcurl
  Threads::Threads
  Boost::system
)
CMAKE

echo "[CHIMERA] MODE B WS + BRIDGE INSTALLED"
