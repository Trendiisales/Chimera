#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "chimera/governance/StrategyFitnessEngine.hpp"
#include "chimera/governance/CorrelationGovernor.hpp"
#include "chimera/control/RegimeClassifier.hpp"

namespace chimera {

struct AllocationStats {
    double weight = 0.0;          // 0.0 - 1.0
    double score = 0.0;           // composite
    double sharpe_like = 0.0;     // pnl / volatility
    double drawdown = 0.0;
    double correlation_penalty = 0.0;
    bool enabled = true;
};

class CapitalAllocator {
public:
    CapitalAllocator(
        StrategyFitnessEngine& fitness,
        CorrelationGovernor& corr,
        RegimeClassifier& regime
    );

    void setBaseCapital(double cap);
    void setMinWeight(double w);
    void setMaxWeight(double w);

    void registerEngine(const std::string& engine);

    void onFill(
        const std::string& engine,
        double pnl
    );

    void rebalance();

    double capitalFor(
        const std::string& engine
    ) const;

    const AllocationStats&
    stats(const std::string& engine) const;

private:
    double computeScore(
        const std::string& engine
    );

    double computeVolatility(
        const std::string& engine
    ) const;

private:
    StrategyFitnessEngine& fitness_engine;
    CorrelationGovernor& corr_governor;
    RegimeClassifier& regime_classifier;

    double base_capital = 1.0;
    double min_weight = 0.05;
    double max_weight = 0.7;

    mutable std::mutex mtx;

    std::unordered_map<
        std::string,
        AllocationStats
    > alloc;

    std::unordered_map<
        std::string,
        std::vector<double>
    > pnl_history;
};

}
