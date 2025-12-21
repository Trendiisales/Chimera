#pragma once

#include <string>
#include <unordered_map>
#include <atomic>

class MicrostructureEngine;
class ExecutionEngine;

class StrategyEngine {
public:
    // ENGINE-MAIN (no execution)
    explicit StrategyEngine(MicrostructureEngine& micro);

    // FULL (execution-enabled)
    StrategyEngine(MicrostructureEngine& micro, ExecutionEngine& exec);

    void update();

    double total_pnl() const;
    const std::unordered_map<std::string,double>& per_strategy_pnl() const;

private:
    MicrostructureEngine& micro_;
    ExecutionEngine* exec_; // nullable in engine-main

    std::unordered_map<std::string,double> pnl_;
    std::atomic<double> total_{0.0};

    void run_for_symbol(const std::string& symbol);

    void strategy_spread_capture(const std::string& symbol);
    void strategy_mean_revert(const std::string& symbol);
};
