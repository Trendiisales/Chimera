#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e
cd ~/chimera

############################################
# TELEMETRY BUS (AUTHORITATIVE)
############################################
cat << 'CPP' > telemetry/TelemetryBus.hpp
#pragma once
#include <string>
#include <vector>
#include <mutex>

struct TelemetryEngineRow {
    std::string symbol;
    double net_bps;
    double dd_bps;
    int trades;
    double fees;
    double alloc;
    double leverage;
    std::string state;
};

struct TelemetryTradeRow {
    std::string engine;
    std::string symbol;
    std::string side;
    double bps;
    double latency_ms;
    double leverage;
};

class TelemetryBus {
public:
    static TelemetryBus& instance() {
        static TelemetryBus bus;
        return bus;
    }

    void updateEngine(const TelemetryEngineRow& row) {
        std::lock_guard<std::mutex> g(mu_);
        bool found = false;
        for (auto& r : engines_) {
            if (r.symbol == row.symbol) {
                r = row;
                found = true;
                break;
            }
        }
        if (!found) {
            engines_.push_back(row);
        }
    }

    void addTrade(const TelemetryTradeRow& row) {
        std::lock_guard<std::mutex> g(mu_);
        trades_.push_back(row);
        if (trades_.size() > 20) {
            trades_.erase(trades_.begin());
        }
    }

    std::vector<TelemetryEngineRow> engines() {
        std::lock_guard<std::mutex> g(mu_);
        return engines_;
    }

    std::vector<TelemetryTradeRow> trades() {
        std::lock_guard<std::mutex> g(mu_);
        return trades_;
    }

private:
    std::mutex mu_;
    std::vector<TelemetryEngineRow> engines_;
    std::vector<TelemetryTradeRow> trades_;
};
CPP

############################################
# SYMBOL LANE (ENGINE PRODUCER)
############################################
cat << 'CPP' > core/SymbolLane_ANTIPARALYSIS.hpp
#pragma once
#include <string>
#include "telemetry/TelemetryBus.hpp"
#include "tier3/TickData.hpp"

class SymbolLane {
public:
    explicit SymbolLane(const std::string& sym)
        : symbol_(sym) {}

    void onTick(const tier3::TickData&) {
        TelemetryEngineRow row;
        row.symbol = symbol_;
        row.net_bps = net_bps_;
        row.dd_bps = dd_bps_;
        row.trades = trade_count_;
        row.fees = fees_paid_;
        row.alloc = alloc_;
        row.leverage = leverage_;
        row.state = "LIVE";

        TelemetryBus::instance().updateEngine(row);
    }

private:
    std::string symbol_;
    double net_bps_ = 0.0;
    double dd_bps_ = 0.0;
    int trade_count_ = 0;
    double fees_paid_ = 0.0;
    double alloc_ = 1.0;
    double leverage_ = 1.0;
};
CPP

############################################
# SHADOW EXECUTOR (TRADE PRODUCER)
############################################
cat << 'CPP' > execution/ShadowExecutor.hpp
#pragma once
#include <string>

class ShadowExecutor {
public:
    ShadowExecutor() = default;

    void onIntent(
        const std::string& engine,
        const std::string& symbol,
        double bps,
        double latency_ms
    );
};
CPP

cat << 'CPP' > execution/ShadowExecutor.cpp
#include "execution/ShadowExecutor.hpp"
#include "telemetry/TelemetryBus.hpp"

void ShadowExecutor::onIntent(
    const std::string& engine,
    const std::string& symbol,
    double bps,
    double latency_ms
) {
    TelemetryTradeRow row;
    row.engine = engine;
    row.symbol = symbol;
    row.side = "BUY";
    row.bps = bps;
    row.latency_ms = latency_ms;
    row.leverage = 1.0;

    TelemetryBus::instance().addTrade(row);
}
CPP

############################################
# TELEMETRY SERVER (JSON EMITTER)
############################################
cat << 'CPP' > telemetry/TelemetryServer.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include "telemetry/TelemetryBus.hpp"

void runTelemetryServer() {
    while (true) {
        auto engines = TelemetryBus::instance().engines();
        auto trades = TelemetryBus::instance().trades();

        std::cout << "{";
        std::cout << "\"engines\":[";

        for (size_t i = 0; i < engines.size(); ++i) {
            const auto& e = engines[i];
            std::cout << "{"
                      << "\"symbol\":\"" << e.symbol << "\","
                      << "\"net_bps\":" << e.net_bps << ","
                      << "\"dd_bps\":" << e.dd_bps << ","
                      << "\"trades\":" << e.trades << ","
                      << "\"fees\":" << e.fees << ","
                      << "\"alloc\":" << e.alloc << ","
                      << "\"leverage\":" << e.leverage << ","
                      << "\"state\":\"" << e.state << "\""
                      << "}";
            if (i + 1 < engines.size()) std::cout << ",";
        }

        std::cout << "],\"trades\":[";

        for (size_t i = 0; i < trades.size(); ++i) {
            const auto& t = trades[i];
            std::cout << "{"
                      << "\"engine\":\"" << t.engine << "\","
                      << "\"symbol\":\"" << t.symbol << "\","
                      << "\"side\":\"" << t.side << "\","
                      << "\"bps\":" << t.bps << ","
                      << "\"latency_ms\":" << t.latency_ms << ","
                      << "\"leverage\":" << t.leverage
                      << "}";
            if (i + 1 < trades.size()) std::cout << ",";
        }

        std::cout << "]}" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}
CPP

############################################
# MAIN (CLEAN, NO INLINE JSON, NO GARBAGE)
############################################
cat << 'CPP' > main.cpp
#include <thread>
#include <iostream>
#include <chrono>

#include "telemetry/TelemetryBus.hpp"
#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "execution/ShadowExecutor.hpp"

void runTelemetryServer();

int main() {
    std::cout << "[CHIMERA] MODE B LIVE STACK | SHADOW EXEC | TELEMETRY ACTIVE" << std::endl;

    std::thread telemetry_thread(runTelemetryServer);

    SymbolLane eth("ETH_PERP");
    SymbolLane btc("BTC_PERP");
    SymbolLane sol("SOL_SPOT");

    ShadowExecutor exec;
    tier3::TickData t{};

    while (true) {
        eth.onTick(t);
        btc.onTick(t);
        sol.onTick(t);

        exec.onIntent("FADE", "ETH_PERP", 2.5, 25.0);

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    telemetry_thread.join();
    return 0;
}
CPP

############################################
# CLEAN BUILD
############################################
rm -rf build
cmake -B build
cmake --build build -j
./build/chimera
