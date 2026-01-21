#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

mkdir -p account exchange router engines risk tier3 metrics logs build

########################################
# metrics/EngineHealth.hpp
########################################
cat > metrics/EngineHealth.hpp << 'EOT'
#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <iostream>

enum class HealthState {
    GREEN,
    YELLOW,
    RED,
    DISABLED
};

struct TradeSample {
    double pnl;
    double latency_ms;
};

struct HealthStats {
    HealthState state = HealthState::YELLOW;
    std::deque<TradeSample> window;
    double pnl_sum = 0;
    double lat_sum = 0;
};

class EngineHealth {
public:
    EngineHealth(size_t window_size, double min_pnl_per_ms)
        : window_size_(window_size),
          min_pnl_per_ms_(min_pnl_per_ms) {}

    void record(const std::string& engine, double pnl, double latency_ms) {
        std::lock_guard<std::mutex> g(mu_);
        auto& h = engines_[engine];

        h.window.push_back({pnl, latency_ms});
        h.pnl_sum += pnl;
        h.lat_sum += latency_ms;

        if (h.window.size() > window_size_) {
            h.pnl_sum -= h.window.front().pnl;
            h.lat_sum -= h.window.front().latency_ms;
            h.window.pop_front();
        }

        updateState(engine, h);
    }

    bool allow(const std::string& engine) {
        std::lock_guard<std::mutex> g(mu_);
        return engines_[engine].state != HealthState::DISABLED;
    }

    void print() {
        std::lock_guard<std::mutex> g(mu_);
        std::cout << "\n=== ENGINE HEALTH ===\n";
        for (auto& kv : engines_) {
            auto& h = kv.second;
            double ppm = (h.lat_sum > 0) ? (h.pnl_sum / h.lat_sum) : 0;
            std::cout << kv.first
                      << " state=" << stateName(h.state)
                      << " pnl/ms=" << ppm
                      << " trades=" << h.window.size()
                      << "\n";
        }
    }

private:
    void updateState(const std::string& name, HealthStats& h) {
        double ppm = (h.lat_sum > 0) ? (h.pnl_sum / h.lat_sum) : 0;

        HealthState prev = h.state;

        if (h.window.size() < window_size_ / 2) {
            h.state = HealthState::YELLOW;
        } else if (ppm > min_pnl_per_ms_) {
            h.state = HealthState::GREEN;
        } else if (ppm > 0) {
            h.state = HealthState::YELLOW;
        } else if (ppm < 0) {
            h.state = HealthState::RED;
        }

        if (h.state == HealthState::RED && prev == HealthState::RED) {
            h.state = HealthState::DISABLED;
        }

        if (h.state != prev) {
            std::cout << "[HEALTH] " << name
                      << " -> " << stateName(h.state)
                      << "\n";
        }
    }

    const char* stateName(HealthState s) const {
        switch (s) {
            case HealthState::GREEN: return "GREEN";
            case HealthState::YELLOW: return "YELLOW";
            case HealthState::RED: return "RED";
            case HealthState::DISABLED: return "DISABLED";
        }
        return "UNKNOWN";
    }

    size_t window_size_;
    double min_pnl_per_ms_;
    std::mutex mu_;
    std::unordered_map<std::string, HealthStats> engines_;
};
EOT

########################################
# router/CapitalRouter.hpp (AUTO-GATED)
########################################
cat > router/CapitalRouter.hpp << 'EOT'
#pragma once
#include <string>
#include <iostream>

#include "../account/OrderLedger.hpp"
#include "../account/PositionEngine.hpp"
#include "../risk/KillSwitchGovernor.hpp"
#include "../tier3/TickData.hpp"
#include "../metrics/TradeJournal.hpp"
#include "../metrics/EngineStats.hpp"
#include "../metrics/EngineHealth.hpp"

class CapitalRouter {
public:
    CapitalRouter(
        OrderLedger* l,
        PositionEngine* p,
        KillSwitchGovernor* k,
        TradeJournal* j,
        EngineStats* s,
        EngineHealth* h
    )
        : ledger_(l), pos_(p), kill_(k),
          journal_(j), stats_(s), health_(h) {}

    void send(
        const std::string& engine,
        const std::string& symbol,
        bool is_buy,
        double qty,
        const tier3::TickData& t,
        double one_way_ms,
        const std::string& session
    ) {
        if (!kill_->globalEnabled()) return;
        if (!health_->allow(engine)) {
            std::cout << "[GATE] " << engine << " BLOCKED (DISABLED)\n";
            return;
        }

        double fill_px = is_buy ? t.ask : t.bid; // worst side
        uint64_t id = ledger_->create(engine, symbol, is_buy, qty, fill_px);
        ledger_->fill(id);

        double before = pos_->getUnrealized(symbol, t.midprice());
        pos_->onFill(symbol, is_buy, qty, fill_px);
        double after = pos_->getUnrealized(symbol, t.midprice());
        double trade_pnl = after - before;

        journal_->log(
            t.exchange_time_us,
            symbol,
            engine,
            is_buy ? "BUY" : "SELL",
            qty,
            fill_px,
            t.spread_bps,
            one_way_ms,
            t.ofi_z,
            t.impulse_bps,
            session
        );

        stats_->record(engine, trade_pnl);
        health_->record(engine, trade_pnl, one_way_ms);

        std::cout << "[TRADE] " << engine
                  << " pnl=" << trade_pnl
                  << " lat=" << one_way_ms << "ms\n";
    }

private:
    OrderLedger* ledger_;
    PositionEngine* pos_;
    KillSwitchGovernor* kill_;
    TradeJournal* journal_;
    EngineStats* stats_;
    EngineHealth* health_;
};
EOT

########################################
# main.cpp (AUTO-GATE LOOP)
########################################
cat > main.cpp << 'EOT'
#include <iostream>
#include <thread>
#include <chrono>

#include "exchange/BinanceWSClient.hpp"
#include "engines/FadeETH_WORKING.hpp"
#include "engines/CascadeBTC_WORKING.hpp"
#include "router/CapitalRouter.hpp"
#include "account/OrderLedger.hpp"
#include "account/PositionEngine.hpp"
#include "metrics/TradeJournal.hpp"
#include "metrics/EngineStats.hpp"
#include "metrics/EngineHealth.hpp"
#include "risk/KillSwitchGovernor.hpp"

static uint64_t now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string sessionTag() {
    using namespace std::chrono;
    auto h = duration_cast<hours>(
        system_clock::now().time_since_epoch()).count() % 24;
    if (h >= 0 && h < 8) return "ASIA";
    if (h >= 8 && h < 16) return "LONDON";
    return "NY";
}

int main() {
    std::cout << "[CHIMERA] AUTO-GATE AUDIT MODE — LIVE MARKET, SHADOW FILLS\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;
    TradeJournal journal("logs/trades.csv");
    EngineStats stats;
    EngineHealth health(20, 0.00001); // window=20 trades, min pnl/ms

    CapitalRouter router(&ledger, &positions, &kill, &journal, &stats, &health);

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    BinanceWSClient ws("ethusdt@bookTicker/ethusdt@aggTrade/btcusdt@bookTicker/btcusdt@aggTrade");

    ws.onTick([&](const std::string& sym, const tier3::TickData& t) {
        uint64_t now = now_us();
        double one_way_ms = (now - t.exchange_time_us) / 1000.0;
        std::string sess = sessionTag();

        eth.onTick(t);
        btc.onTick(t);

        if (sym == "ETHUSDT" && eth.hasSignal()) {
            auto s = eth.consumeSignal();
            router.send("ETH_FADE", sym, s.is_buy, 0.01, t, one_way_ms, sess);
        }

        if (sym == "BTCUSDT" && btc.hasSignal()) {
            auto s = btc.consumeSignal();
            router.send("BTC_CASCADE", sym, s.is_buy, 0.001, t, one_way_ms, sess);
        }
    });

    ws.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        stats.print();
        health.print();
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

find_package(Boost REQUIRED COMPONENTS system)

include_directories(
    .
    engines
    account
    exchange
    router
    risk
    tier3
    metrics
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

echo "[OK] AUTO-GATE MODE FILES WRITTEN"
