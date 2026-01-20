#!/usr/bin/env bash
set -e

echo "[CHIMERA] Installing latency-based KillSwitch governor"

# ============================================================
# risk/KillSwitchGovernor.hpp
# ============================================================
cat > risk/KillSwitchGovernor.hpp << 'KILL'
#pragma once

#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <iostream>

class KillSwitchGovernor {
public:
    KillSwitchGovernor() = default;

    inline void registerEngine(const std::string& name) {
        std::lock_guard<std::mutex> g(mu_);
        engines_[name] = true;
    }

    inline void recordSignal(const std::string& engine, uint64_t) {
        std::lock_guard<std::mutex> g(mu_);
        if (engines_.count(engine)) {
            last_engine_ = engine;
        }
    }

    inline bool globalEnabled() const {
        return global_enabled_.load(std::memory_order_relaxed);
    }

    inline bool isEngineEnabled(const std::string& engine) const {
        auto it = engines_.find(engine);
        if (it == engines_.end()) return false;
        return it->second && global_enabled_.load(std::memory_order_relaxed);
    }

    inline double scaleSize(const std::string&, double raw) const {
        return raw * risk_scale_.load(std::memory_order_relaxed);
    }

    inline void setGlobalEnabled(bool v) {
        bool prev = global_enabled_.exchange(v, std::memory_order_relaxed);
        if (prev != v) {
            if (!v) {
                std::cerr << "[RISK] GLOBAL FREEZE ENABLED\n";
            } else {
                std::cerr << "[RISK] GLOBAL TRADING RESUMED\n";
            }
        }
    }

    inline void setRiskScale(double v) {
        double prev = risk_scale_.exchange(v, std::memory_order_relaxed);
        if (prev != v) {
            std::cerr << "[RISK] SCALE -> " << v << "\n";
        }
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, bool> engines_;
    std::string last_engine_;

    std::atomic<bool> global_enabled_{true};
    std::atomic<double> risk_scale_{1.0};
};
KILL

# ============================================================
# main.cpp (latency governor logic)
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
#include <atomic>

static bool allow_print(std::atomic<uint64_t>& last_us, uint64_t interval_us) {
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    uint64_t prev = last_us.load(std::memory_order_relaxed);
    if (now - prev < interval_us) return false;
    last_us.store(now, std::memory_order_relaxed);
    return true;
}

// ============================================================
// LATENCY GOVERNOR
// ============================================================
struct LatencyGovernor {
    int good = 0;
    int warn = 0;
    int bad = 0;

    void update(double one_way_ms, KillSwitchGovernor& kill) {
        // Reset counters
        if (one_way_ms <= 8.0) {
            good++;
            warn = bad = 0;
        } else if (one_way_ms <= 15.0) {
            warn++;
            good = bad = 0;
        } else {
            bad++;
            good = warn = 0;
        }

        // Sustained state change (3 samples)
        if (good >= 3) {
            kill.setGlobalEnabled(true);
            kill.setRiskScale(1.0);
        } else if (warn >= 3) {
            kill.setGlobalEnabled(true);
            kill.setRiskScale(0.5);
        } else if (bad >= 3) {
            kill.setRiskScale(0.0);
            kill.setGlobalEnabled(false);
        }
    }
};

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

    SymbolLane lane_eth("ETHUSDT", 0.001, 9101, portfolio, (void*)&eth_exec, corr);
    SymbolLane lane_btc("BTCUSDT", 0.001, 9102, portfolio, (void*)&btc_exec, corr);

    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;

    BinanceWSClient ws(
        "stream.binance.com",
        "9443",
        "btcusdt@bookTicker/ethusdt@bookTicker/btcusdt@aggTrade/ethusdt@aggTrade"
    );

    std::atomic<uint64_t> last_eth_log{0};
    std::atomic<uint64_t> last_btc_log{0};

    const uint64_t LOG_INTERVAL_US = 500000;

    LatencyGovernor latgov;

    ws.setCallback([&](const std::string& stream,
                       const tier3::TickData& t) {
        double rtt_us = static_cast<double>(t.exchange_time_us);
        double one_way_ms = (rtt_us * 0.5) / 1000.0;

        // Update governor once per tick batch
        latgov.update(one_way_ms, kill);

        if (stream.find("ethusdt") != std::string::npos) {
            lane_eth.onTick(t);
            eth.onTick(t);

            if (allow_print(last_eth_log, LOG_INTERVAL_US)) {
                std::cout << "[LAT] ETHUSDT "
                          << "one-way=" << one_way_ms << "ms "
                          << "spr=" << t.spread_bps
                          << " ofi=" << t.ofi_z << std::endl;
            }

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

            if (allow_print(last_btc_log, LOG_INTERVAL_US)) {
                std::cout << "[LAT] BTCUSDT "
                          << "one-way=" << one_way_ms << "ms "
                          << "spr=" << t.spread_bps
                          << " ofi=" << t.ofi_z << std::endl;
            }

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
              << (keys.dry_run ? "DRY" : "LIVE")
              << " | LATENCY GOVERNOR ACTIVE"
              << std::endl;

    ws.start();
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(10));
}
MAIN

echo "[CHIMERA] Kill-latency governor installed"
