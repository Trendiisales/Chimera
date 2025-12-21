#pragma once

#include "micro/MicrostructureEngine.hpp"
#include "execution/ExecutionEngine.hpp"

#include <string>
#include <unordered_map>
#include <atomic>

class StrategyEngine {
public:
    StrategyEngine(
        MicrostructureEngine& micro,
        ExecutionEngine& exec
    );

    void update();

    double total_pnl() const;
    const std::unordered_map<std::string,double>& per_strategy_pnl() const;

private:
    MicrostructureEngine& micro_;
    ExecutionEngine& exec_;

    std::unordered_map<std::string,double> pnl_;
    std::atomic<double> total_{0.0};

    void run_for_symbol(const std::string& symbol);

    void strategy_spread_capture(const std::string& symbol);
    void strategy_mean_revert(const std::string& symbol);
};
