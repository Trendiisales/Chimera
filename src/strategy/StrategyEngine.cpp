#include "strategy/StrategyEngine.hpp"
#include "latency/LatencyRegistry.hpp"

#include <chrono>

using clock_type = std::chrono::steady_clock;

StrategyEngine::StrategyEngine(
    MicrostructureEngine& micro,
    ExecutionEngine& exec
)
: micro_(micro),
  exec_(exec)
{}

void StrategyEngine::update() {
    auto t_start = clock_type::now();

    for (const auto& kv : micro_.symbols()) {
        run_for_symbol(kv.first);
    }

    auto t_end = clock_type::now();
    g_latency.strategy_to_exec_ns.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t_end - t_start
        ).count(),
        std::memory_order_relaxed
    );
}

void StrategyEngine::run_for_symbol(const std::string& symbol) {
    strategy_spread_capture(symbol);
    strategy_mean_revert(symbol);
}

void StrategyEngine::strategy_spread_capture(const std::string& symbol) {
    double spread_bps = micro_.spread_bps(symbol);
    if (spread_bps > 5.0) {
        exec_.submit_intent(symbol, "BUY", micro_.mid(symbol), 0.01);
    }
}

void StrategyEngine::strategy_mean_revert(const std::string& symbol) {
    double spread_bps = micro_.spread_bps(symbol);
    if (spread_bps < 1.0) {
        exec_.submit_intent(symbol, "SELL", micro_.mid(symbol), 0.01);
    }
}

double StrategyEngine::total_pnl() const {
    return total_.load(std::memory_order_relaxed);
}

const std::unordered_map<std::string,double>&
StrategyEngine::per_strategy_pnl() const {
    return pnl_;
}
