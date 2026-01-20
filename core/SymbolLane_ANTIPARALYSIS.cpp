#include "../telemetry/TelemetryBus.hpp"
#include "SymbolLane_ANTIPARALYSIS.hpp"

#include <iostream>
#include <chrono>
#include <thread>

using steady_clock = std::chrono::steady_clock;

SymbolLane::SymbolLane(
    const std::string& symbol,
    double min_size,
    int,
    PortfolioGovernor*,
    void*,
    std::shared_ptr<CorrelationGovernor>
)
    : symbol_(symbol),
      min_size_(min_size),
      tick_count_(0)
{
    kill_gov_ = std::make_unique<KillSwitchGovernor>();
    metrics_server_ = nullptr;
    funding_sniper_ = nullptr;
}

void SymbolLane::onTick(const tier3::TickData&) {
    tick_count_.fetch_add(1, std::memory_order_relaxed);
    static auto last_pub = std::chrono::steady_clock::now();
    auto now_pub = std::chrono::steady_clock::now();
    if (now_pub - last_pub >= std::chrono::seconds(1)) {
        last_pub = now_pub;
        TelemetryBus::instance().updateEngine({
            symbol_,
            net_bps_,
            dd_bps_,
            trade_count_,
            fees_paid_,
            alloc_,
            leverage_,
            "LIVE"
        });
    }

    static steady_clock::time_point last_heartbeat = steady_clock::now();
    auto now = steady_clock::now();

    if (now - last_heartbeat >= std::chrono::minutes(1)) {
        last_heartbeat = now;

        std::cout << "\n===== HEARTBEAT [" << symbol_ << "] =====\n";
        std::cout << "Ticks: " << tick_count_.load() << "\n";
        std::cout << "MinSize: " << min_size_ << "\n";
    }
}

DecisionResult SymbolLane::applyRiskAndRoute(
    const std::string&,
    bool,
    double confidence,
    double
) {
    DecisionResult r;
    r.allowed = false;
    r.size = 0.0;

    if (!kill_gov_->globalEnabled()) return r;
    if (confidence < 0.05) return r;

    r.allowed = true;
    r.size = min_size_ * confidence;
    return r;
}

void SymbolLane::run() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
