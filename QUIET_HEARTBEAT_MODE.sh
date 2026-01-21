#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

########################################
# router/CapitalRouter.hpp (QUIET)
########################################
cat > router/CapitalRouter.hpp << 'EOT'
#pragma once
#include <string>

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
        if (!health_->allow(engine)) return;

        double fill_px = is_buy ? t.ask : t.bid;
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
# main.cpp (1-MIN HEARTBEAT ONLY)
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
    std::cout << "[CHIMERA] AUTO-GATE AUDIT MODE — QUIET HEARTBEAT\n";

    KillSwitchGovernor kill;
    OrderLedger ledger;
    PositionEngine positions;
    TradeJournal journal("logs/trades.csv");
    EngineStats stats;
    EngineHealth health(20, 0.00001);

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
        std::this_thread::sleep_for(std::chrono::minutes(1));

        std::cout << "\n===== HEARTBEAT =====\n";
        stats.print();
        health.print();
        std::cout << "Trades logged to: logs/trades.csv\n";
    }
}
EOT

echo "[OK] QUIET HEARTBEAT MODE ENABLED"
