#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

mkdir -p account exchange router engines risk tier3 metrics build logs

########################################
# metrics/TradeJournal.hpp
########################################
cat > metrics/TradeJournal.hpp << 'EOT'
#pragma once
#include <fstream>
#include <mutex>
#include <string>

class TradeJournal {
public:
    TradeJournal(const std::string& path) {
        file_.open(path, std::ios::out | std::ios::app);
        if (file_.tellp() == 0) {
            file_ << "ts_us,symbol,engine,side,qty,fill_px,spread_bps,one_way_ms,ofi,impulse,session\n";
        }
    }

    void log(
        uint64_t ts_us,
        const std::string& symbol,
        const std::string& engine,
        const std::string& side,
        double qty,
        double fill_px,
        double spread_bps,
        double one_way_ms,
        double ofi,
        double impulse,
        const std::string& session
    ) {
        std::lock_guard<std::mutex> g(mu_);
        file_ << ts_us << ","
              << symbol << ","
              << engine << ","
              << side << ","
              << qty << ","
              << fill_px << ","
              << spread_bps << ","
              << one_way_ms << ","
              << ofi << ","
              << impulse << ","
              << session << "\n";
        file_.flush();
    }

private:
    std::ofstream file_;
    std::mutex mu_;
};
EOT

########################################
# metrics/EngineStats.hpp
########################################
cat > metrics/EngineStats.hpp << 'EOT'
#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <iostream>

struct EngineStat {
    int trades = 0;
    double pnl = 0;
    double wins = 0;
};

class EngineStats {
public:
    void record(const std::string& engine, double trade_pnl) {
        std::lock_guard<std::mutex> g(mu_);
        auto& s = stats_[engine];
        s.trades++;
        s.pnl += trade_pnl;
        if (trade_pnl > 0) s.wins++;
    }

    void print() {
        std::lock_guard<std::mutex> g(mu_);
        std::cout << "\n=== ENGINE SCOREBOARD ===\n";
        for (auto& kv : stats_) {
            auto& s = kv.second;
            double winrate = s.trades ? (s.wins / s.trades * 100.0) : 0.0;
            std::cout << kv.first
                      << " trades=" << s.trades
                      << " pnl=" << s.pnl
                      << " win%=" << winrate << "\n";
        }
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, EngineStat> stats_;
};
EOT

########################################
# router/CapitalRouter.hpp (AUDIT SHADOW)
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

class CapitalRouter {
public:
    CapitalRouter(
        OrderLedger* l,
        PositionEngine* p,
        KillSwitchGovernor* k,
        TradeJournal* j,
        EngineStats* s
    )
        : ledger_(l), pos_(p), kill_(k), journal_(j), stats_(s) {}

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

        double fill_px = is_buy ? t.ask : t.bid; // WORST SIDE
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

        std::cout << "[AUDIT] " << engine
                  << " " << symbol
                  << " pnl=" << trade_pnl
                  << " lat=" << one_way_ms << "ms\n";
    }

private:
    OrderLedger* ledger_;
    PositionEngine* pos_;
    KillSwitchGovernor* kill_;
    TradeJournal* journal_;
    EngineStats* stats_;
};
EOT

########################################
# Patch account/PositionEngine.hpp
########################################
cat > account/PositionEngine.hpp << 'EOT'
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

struct Position {
    double qty = 0;
    double avg_px = 0;
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

        p.qty += signed_qty;
        if (p.qty == 0) p.avg_px = 0;
    }

    double getUnrealized(const std::string& sym, double mid) {
        std::lock_guard<std::mutex> g(mu_);
        auto it = pos_.find(sym);
        if (it == pos_.end()) return 0;
        auto& p = it->second;
        return p.qty * (mid - p.avg_px);
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, Position> pos_;
};
EOT

########################################
# main.cpp (AUDIT MODE)
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
    std::cout << "[CHIMERA] AUDIT MODE — LIVE MARKET, SHADOW FILLS\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;
    TradeJournal journal("logs/trades.csv");
    EngineStats stats;

    CapitalRouter router(&ledger, &positions, &kill, &journal, &stats);

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

echo "[OK] AUDIT MODE FILES WRITTEN"
