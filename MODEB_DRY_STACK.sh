#!/usr/bin/env bash
set -e

echo "[CHIMERA] Installing MODE B DRY TRADE LOOP"

mkdir -p account exchange router core risk engines build

############################################
# account/OrderLedger.hpp
############################################
cat << 'EOT' > account/OrderLedger.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

enum class OrderStatus {
    PENDING,
    ACKED,
    FILLED,
    CANCELED,
    REJECTED
};

struct OrderRecord {
    uint64_t id;
    std::string engine;
    std::string symbol;
    bool is_buy;
    double qty;
    double price;
    uint64_t ts_sent;
    uint64_t ts_fill;
    OrderStatus status;
};

class OrderLedger {
public:
    uint64_t create(
        const std::string& engine,
        const std::string& symbol,
        bool is_buy,
        double qty,
        double price,
        uint64_t ts
    ) {
        std::lock_guard<std::mutex> g(mu_);
        uint64_t id = ++next_id_;
        orders_[id] = {id, engine, symbol, is_buy, qty, price, ts, 0, OrderStatus::PENDING};
        return id;
    }

    void ack(uint64_t id) {
        std::lock_guard<std::mutex> g(mu_);
        if (orders_.count(id)) orders_[id].status = OrderStatus::ACKED;
    }

    void fill(uint64_t id, uint64_t ts) {
        std::lock_guard<std::mutex> g(mu_);
        if (orders_.count(id)) {
            orders_[id].status = OrderStatus::FILLED;
            orders_[id].ts_fill = ts;
        }
    }

    std::unordered_map<uint64_t, OrderRecord> snapshot() const {
        std::lock_guard<std::mutex> g(mu_);
        return orders_;
    }

private:
    mutable std::mutex mu_;
    uint64_t next_id_{0};
    std::unordered_map<uint64_t, OrderRecord> orders_;
};
EOT

############################################
# account/PositionEngine.hpp
############################################
cat << 'EOT' > account/PositionEngine.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

struct Position {
    double qty{0};
    double avg_px{0};
    double realized_pnl{0};
};

class PositionEngine {
public:
    void onFill(
        const std::string& symbol,
        bool is_buy,
        double qty,
        double px
    ) {
        std::lock_guard<std::mutex> g(mu_);
        auto& p = pos_[symbol];

        double signed_qty = is_buy ? qty : -qty;
        double new_qty = p.qty + signed_qty;

        if (p.qty == 0) {
            p.avg_px = px;
            p.qty = new_qty;
            return;
        }

        if ((p.qty > 0 && signed_qty > 0) ||
            (p.qty < 0 && signed_qty < 0)) {
            p.avg_px =
                ((p.avg_px * std::abs(p.qty)) +
                 (px * std::abs(signed_qty))) /
                std::abs(new_qty);
            p.qty = new_qty;
        } else {
            double closed = std::min(std::abs(p.qty), std::abs(signed_qty));
            double pnl = closed * (px - p.avg_px);
            if (p.qty > 0) pnl = -pnl;
            p.realized_pnl += pnl;
            p.qty = new_qty;
            if (p.qty == 0) p.avg_px = 0;
        }
    }

    std::unordered_map<std::string, Position> snapshot() const {
        std::lock_guard<std::mutex> g(mu_);
        return pos_;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Position> pos_;
};
EOT

############################################
# exchange/BinanceUserStream.hpp
############################################
cat << 'EOT' > exchange/BinanceUserStream.hpp
#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include "../account/OrderLedger.hpp"

class BinanceUserStream {
public:
    using FillCB = std::function<void(uint64_t)>;

    BinanceUserStream(OrderLedger* ledger)
        : ledger_(ledger) {}

    void onFill(FillCB cb) {
        fill_cb_ = cb;
    }

    void start() {
        running_.store(true);
        worker_ = std::thread([this]() {
            while (running_) {
                auto snap = ledger_->snapshot();
                for (auto& kv : snap) {
                    if (kv.second.status == OrderStatus::PENDING) {
                        ledger_->ack(kv.first);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        ledger_->fill(kv.first, now());
                        if (fill_cb_) fill_cb_(kv.first);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    void stop() {
        running_.store(false);
        if (worker_.joinable()) worker_.join();
    }

private:
    uint64_t now() const {
        return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    OrderLedger* ledger_;
    FillCB fill_cb_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
EOT

############################################
# router/CapitalRouter.cpp
############################################
cat << 'EOT' > router/CapitalRouter.cpp
#include <iostream>
#include "../account/OrderLedger.hpp"
#include "../risk/KillSwitchGovernor.hpp"

class CapitalRouter {
public:
    CapitalRouter(OrderLedger* ledger, KillSwitchGovernor* kill)
        : ledger_(ledger), kill_(kill) {}

    void send(
        const std::string& engine,
        const std::string& symbol,
        bool is_buy,
        double qty,
        double price
    ) {
        if (!kill_->globalEnabled()) {
            std::cout << "[ROUTER] BLOCKED BY KILL SWITCH\n";
            return;
        }

        uint64_t id = ledger_->create(
            engine, symbol, is_buy, qty, price, now()
        );

        std::cout << "[ORDER] id=" << id
                  << " " << engine
                  << " " << symbol
                  << (is_buy ? " BUY " : " SELL ")
                  << qty << " @ " << price
                  << " DRY\n";
    }

private:
    uint64_t now() const {
        return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    OrderLedger* ledger_;
    KillSwitchGovernor* kill_;
};
EOT

############################################
# main.cpp
############################################
cat << 'EOT' > main.cpp
#include <iostream>
#include <thread>

#include "tier3/TickData.hpp"
#include "risk/KillSwitchGovernor.hpp"
#include "account/OrderLedger.hpp"
#include "account/PositionEngine.hpp"
#include "exchange/BinanceUserStream.hpp"

#include "engines/FadeETH_WORKING.hpp"
#include "engines/CascadeBTC_WORKING.hpp"

#include "router/CapitalRouter.cpp"

int main() {
    std::cout << "[CHIMERA] MODE B DRY TRADE LOOP\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;

    CapitalRouter router(&ledger, &kill);

    BinanceUserStream user_stream(&ledger);
    user_stream.onFill([&](uint64_t id) {
        auto snap = ledger.snapshot();
        if (!snap.count(id)) return;
        auto& o = snap[id];
        positions.onFill(o.symbol, o.is_buy, o.qty, o.price);
        std::cout << "[FILL] id=" << id
                  << " " << o.symbol
                  << " qty=" << o.qty
                  << " px=" << o.price << "\n";
    });

    user_stream.start();

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    while (true) {
        tier3::TickData t;
        t.bid = 100;
        t.ask = 101;
        t.spread_bps = 1.0f;
        t.depth_ratio = 0.5f;
        t.ofi_z = 1.2f;
        t.impulse_bps = 12.0f;

        eth.onTick(t);
        btc.onTick(t);

        if (eth.hasSignal()) {
            auto s = eth.consumeSignal();
            router.send("ETH_FADE", "ETHUSDT", s.is_buy, 0.01, s.price);
        }

        if (btc.hasSignal()) {
            auto s = btc.consumeSignal();
            router.send("BTC_CASCADE", "BTCUSDT", s.is_buy, 0.01, s.price);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
EOT

############################################
# CMakeLists.txt
############################################
cat << 'EOT' > CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(chimera)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    .
    tier3
    engines
    account
    exchange
    router
    risk
)

add_executable(chimera
    main.cpp
    router/CapitalRouter.cpp
)
EOT

############################################
# BUILD
############################################
cd build
cmake ..
make -j

echo
echo "[CHIMERA] DRY MODE READY"
echo "Run with: ./chimera"
