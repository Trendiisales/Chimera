#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

mkdir -p account exchange router risk engines tier3 core

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
    ACKED,
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

    void ack(uint64_t id) {
        std::lock_guard<std::mutex> g(mu_);
        if (orders_.count(id)) orders_[id].status = OrderStatus::ACKED;
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

struct Position {
    double qty = 0.0;
    double avg_px = 0.0;
};

class PositionEngine {
public:
    void onFill(const std::string& symbol, bool is_buy, double qty, double px) {
        std::lock_guard<std::mutex> g(mu_);
        auto& p = pos_[symbol];
        double signed_qty = is_buy ? qty : -qty;
        if (p.qty == 0) p.avg_px = px;
        p.qty += signed_qty;
        if (p.qty == 0) p.avg_px = 0;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Position> pos_;
};
EOT

########################################
# exchange/BinanceUserStream.hpp (DRY)
########################################
cat > exchange/BinanceUserStream.hpp << 'EOT'
#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include "../account/OrderLedger.hpp"

class BinanceUserStream {
public:
    explicit BinanceUserStream(OrderLedger* ledger)
        : ledger_(ledger) {}

    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_) {
                auto snap = ledger_->snapshot();
                for (auto& kv : snap) {
                    if (kv.second.status == OrderStatus::PENDING) {
                        ledger_->ack(kv.first);
                        ledger_->fill(kv.first);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

private:
    OrderLedger* ledger_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
EOT

########################################
# router/CapitalRouter.hpp
########################################
cat > router/CapitalRouter.hpp << 'EOT'
#pragma once
#include <string>
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
        if (!kill_->globalEnabled()) return;
        uint64_t id = ledger_->create(engine, symbol, is_buy, qty, price);
        std::cout << "[DRY ORDER] id=" << id
                  << " " << engine
                  << " " << symbol
                  << " qty=" << qty
                  << " px=" << price << "\n";
    }

private:
    OrderLedger* ledger_;
    KillSwitchGovernor* kill_;
};
EOT

########################################
# main.cpp
########################################
cat > main.cpp << 'EOT'
#include <iostream>
#include <thread>

#include "risk/KillSwitchGovernor.hpp"
#include "account/OrderLedger.hpp"
#include "account/PositionEngine.hpp"
#include "exchange/BinanceUserStream.hpp"
#include "router/CapitalRouter.hpp"

int main() {
    std::cout << "[CHIMERA] MODE B DRY LOOP\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;

    CapitalRouter router(&ledger, &kill);
    BinanceUserStream user(&ledger);
    user.start();

    while (true) {
        router.send("ETH_FADE", "ETHUSDT", true, 0.01, 100.0);
        router.send("BTC_CASCADE", "BTCUSDT", false, 0.01, 50000.0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
EOT

########################################
# CMakeLists.txt
########################################
cat > CMakeLists.txt << 'EOT'
cmake_minimum_required(VERSION 3.10)
project(chimera)
set(CMAKE_CXX_STANDARD 17)

include_directories(.)

add_executable(chimera
    main.cpp
)
EOT

echo "[OK] MODE B DRY FILES WRITTEN"
