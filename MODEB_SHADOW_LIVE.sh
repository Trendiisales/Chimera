#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

mkdir -p account exchange router engines risk tier3 build

########################################
# exchange/BinanceWSClient.hpp
########################################
cat > exchange/BinanceWSClient.hpp << 'EOT'
#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>

#include "../tier3/TickData.hpp"

class BinanceWSClient {
public:
    using TickCB = std::function<void(const std::string&, const tier3::TickData&)>;

    BinanceWSClient(const std::string& symbols);
    void onTick(TickCB cb);
    void start();
    void stop();

private:
    void run();

    std::string symbols_;
    TickCB cb_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
EOT

########################################
# exchange/BinanceWSClient.cpp
########################################
cat > exchange/BinanceWSClient.cpp << 'EOT'
#include "BinanceWSClient.hpp"

#include <iostream>
#include <chrono>
#include <cmath>
#include <cstring>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

using tcp = boost::asio::ip::tcp;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using json = nlohmann::json;

BinanceWSClient::BinanceWSClient(const std::string& symbols)
    : symbols_(symbols) {}

void BinanceWSClient::onTick(TickCB cb) {
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

static uint64_t now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void BinanceWSClient::run() {
    try {
        boost::asio::io_context ioc;
        tcp::resolver resolver{ioc};
        websocket::stream<tcp::socket> ws{ioc};

        auto const results = resolver.resolve("stream.binance.com", "9443");
        boost::asio::connect(ws.next_layer(), results.begin(), results.end());

        std::string host = "stream.binance.com";
        std::string target = "/stream?streams=" + symbols_;

        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "chimera-shadow");
            }));

        ws.handshake(host, target);

        beast::flat_buffer buffer;

        while (running_) {
            ws.read(buffer);
            std::string msg = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            auto j = json::parse(msg, nullptr, false);
            if (!j.is_object()) continue;

            if (!j.contains("data")) continue;
            auto& d = j["data"];

            tier3::TickData t;
            t.ts_ns = now_us() * 1000;

            std::string symbol = d["s"].get<std::string>();

            if (d.contains("b") && d.contains("a")) {
                t.bid = std::stof(d["b"].get<std::string>());
                t.ask = std::stof(d["a"].get<std::string>());
                t.spread_bps = (float)((t.ask - t.bid) / ((t.ask + t.bid) * 0.5) * 10000.0);
            }

            if (d.contains("p") && d.contains("q")) {
                double px = std::stod(d["p"].get<std::string>());
                double qty = std::stod(d["q"].get<std::string>());
                t.ofi_z = (float)(qty * (px > t.midprice() ? 1.0 : -1.0));
                t.impulse_bps = (float)(std::abs(px - t.midprice()) / t.midprice() * 10000.0);
            }

            t.exchange_time_us = now_us();
            t.depth_ratio = 0.8f;

            if (cb_) cb_(symbol, t);
        }
    } catch (const std::exception& e) {
        std::cerr << "[WS] ERROR " << e.what() << "\n";
    }
}
EOT

########################################
# account/OrderLedger.hpp
########################################
cat > account/OrderLedger.hpp << 'EOT'
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

enum class OrderStatus {
    PENDING,
    FILLED
};

struct OrderRecord {
    uint64_t id;
    std::string engine;
    std::string symbol;
    bool is_buy;
    double qty;
    double price;
    OrderStatus status;
};

class OrderLedger {
public:
    uint64_t create(
        const std::string& engine,
        const std::string& symbol,
        bool is_buy,
        double qty,
        double price
    ) {
        std::lock_guard<std::mutex> g(mu_);
        uint64_t id = ++next_id_;
        orders_[id] = {id, engine, symbol, is_buy, qty, price, OrderStatus::PENDING};
        return id;
    }

    void fill(uint64_t id) {
        std::lock_guard<std::mutex> g(mu_);
        if (orders_.count(id)) orders_[id].status = OrderStatus::FILLED;
    }

    std::unordered_map<uint64_t, OrderRecord> snapshot() const {
        std::lock_guard<std::mutex> g(mu_);
        return orders_;
    }

private:
    mutable std::mutex mu_;
    uint64_t next_id_ = 0;
    std::unordered_map<uint64_t, OrderRecord> orders_;
};
EOT

########################################
# account/PositionEngine.hpp
########################################
cat > account/PositionEngine.hpp << 'EOT'
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <iostream>

struct Position {
    double qty = 0;
    double avg_px = 0;
    double realized_pnl = 0;
};

class PositionEngine {
public:
    void onFill(const std::string& sym, bool is_buy, double qty, double px) {
        std::lock_guard<std::mutex> g(mu_);
        auto& p = pos_[sym];
        double signed_qty = is_buy ? qty : -qty;

        if (p.qty == 0) {
            p.avg_px = px;
            p.qty = signed_qty;
            return;
        }

        double closing = std::min(std::abs(p.qty), std::abs(signed_qty));
        double pnl = closing * (px - p.avg_px);
        if (p.qty > 0) pnl = -pnl;
        p.realized_pnl += pnl;
        p.qty += signed_qty;

        if (p.qty == 0) p.avg_px = 0;

        std::cout << "[POS] " << sym
                  << " qty=" << p.qty
                  << " avg=" << p.avg_px
                  << " pnl=" << p.realized_pnl << "\n";
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, Position> pos_;
};
EOT

########################################
# router/CapitalRouter.hpp (SHADOW WORST-SIDE FILLS)
########################################
cat > router/CapitalRouter.hpp << 'EOT'
#pragma once
#include <string>
#include <iostream>

#include "../account/OrderLedger.hpp"
#include "../account/PositionEngine.hpp"
#include "../risk/KillSwitchGovernor.hpp"
#include "../tier3/TickData.hpp"

class CapitalRouter {
public:
    CapitalRouter(OrderLedger* l, PositionEngine* p, KillSwitchGovernor* k)
        : ledger_(l), pos_(p), kill_(k) {}

    void send(
        const std::string& engine,
        const std::string& symbol,
        bool is_buy,
        double qty,
        const tier3::TickData& t
    ) {
        if (!kill_->globalEnabled()) return;

        double fill_px = is_buy ? t.ask : t.bid; // WORST SIDE

        uint64_t id = ledger_->create(engine, symbol, is_buy, qty, fill_px);
        ledger_->fill(id);
        pos_->onFill(symbol, is_buy, qty, fill_px);

        std::cout << "[SHADOW FILL] id=" << id
                  << " " << engine
                  << " " << symbol
                  << (is_buy ? " BUY " : " SELL ")
                  << qty << " @ " << fill_px << "\n";
    }

private:
    OrderLedger* ledger_;
    PositionEngine* pos_;
    KillSwitchGovernor* kill_;
};
EOT

########################################
# main.cpp
########################################
cat > main.cpp << 'EOT'
#include <iostream>
#include <thread>

#include "exchange/BinanceWSClient.hpp"
#include "engines/FadeETH_WORKING.hpp"
#include "engines/CascadeBTC_WORKING.hpp"
#include "router/CapitalRouter.hpp"
#include "account/OrderLedger.hpp"
#include "account/PositionEngine.hpp"
#include "risk/KillSwitchGovernor.hpp"

static uint64_t now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    std::cout << "[CHIMERA] SHADOW MODE — LIVE MARKET, DRY EXECUTION (WORST-SIDE)\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;

    CapitalRouter router(&ledger, &positions, &kill);

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    BinanceWSClient ws("ethusdt@bookTicker/ethusdt@aggTrade/btcusdt@bookTicker/btcusdt@aggTrade");

    ws.onTick([&](const std::string& sym, const tier3::TickData& t) {
        static uint64_t last = now_us();
        uint64_t now = now_us();
        double one_way_ms = (now - t.exchange_time_us) / 1000.0;

        std::cout << "[LAT] " << sym
                  << " one-way=" << one_way_ms
                  << "ms spr=" << t.spread_bps
                  << " ofi=" << t.ofi_z << "\n";

        eth.onTick(t);
        btc.onTick(t);

        if (sym == "ETHUSDT" && eth.hasSignal()) {
            auto s = eth.consumeSignal();
            router.send("ETH_FADE", sym, s.is_buy, 0.01, t);
        }

        if (sym == "BTCUSDT" && btc.hasSignal()) {
            auto s = btc.consumeSignal();
            router.send("BTC_CASCADE", sym, s.is_buy, 0.001, t);
        }

        last = now;
    });

    ws.start();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
EOT

########################################
# CMakeLists.txt
########################################
cat > CMakeLists.txt << 'EOT'
cmake_minimum_required(VERSION 3.10)
project(chimera)
set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system)

include_directories(
    .
    engines
    account
    exchange
    router
    risk
    tier3
)

add_executable(chimera
    main.cpp
    exchange/BinanceWSClient.cpp
)

target_link_libraries(chimera
    Boost::system
    pthread
)
EOT

echo "[OK] SHADOW LIVE MODE FILES WRITTEN"
