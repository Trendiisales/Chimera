#include "strategy/StrategyEngine.hpp"
#include "micro/MicrostructureEngine.hpp"
#include "execution/ExecutionEngine.hpp"

StrategyEngine::StrategyEngine(MicrostructureEngine& micro)
: micro_(micro), exec_(nullptr) {
    pnl_["spread_capture"] = 0.0;
    pnl_["mean_revert"]    = 0.0;
}

StrategyEngine::StrategyEngine(MicrostructureEngine& micro, ExecutionEngine& exec)
: micro_(micro), exec_(&exec) {
    pnl_["spread_capture"] = 0.0;
    pnl_["mean_revert"]    = 0.0;
}

void StrategyEngine::update() {
    run_for_symbol("BTCUSDT");
    run_for_symbol("ETHUSDT");

    double sum = 0.0;
    for (const auto& kv : pnl_) sum += kv.second;
    total_.store(sum, std::memory_order_relaxed);
}

void StrategyEngine::run_for_symbol(const std::string& symbol) {
    strategy_spread_capture(symbol);
    strategy_mean_revert(symbol);
}

void StrategyEngine::strategy_spread_capture(const std::string& symbol) {
    double spread_bps = micro_.spread_bps(symbol);
    if (spread_bps > 5.0) {
        pnl_["spread_capture"] += 0.01;

        if (exec_) {
            double mid = micro_.mid(symbol);
            exec_->submit_intent(symbol, "BUY", mid, 0.01);
        }
    }
}

void StrategyEngine::strategy_mean_revert(const std::string& symbol) {
    (void)symbol;
}

double StrategyEngine::total_pnl() const {
    return total_.load(std::memory_order_relaxed);
}

const std::unordered_map<std::string,double>&
StrategyEngine::per_strategy_pnl() const {
    return pnl_;
}
