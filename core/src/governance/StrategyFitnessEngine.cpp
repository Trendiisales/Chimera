#include "chimera/governance/StrategyFitnessEngine.hpp"

namespace chimera {

StrategyFitnessEngine::StrategyFitnessEngine() {}

void StrategyFitnessEngine::setDrawdownLimit(
    double dd
) {
    max_dd_limit = dd;
}

void StrategyFitnessEngine::setMinWinRate(
    double wr
) {
    min_win_rate = wr;
}

void StrategyFitnessEngine::recordTrade(
    const std::string& engine,
    double pnl
) {
    FitnessStats& f = fitness[engine];

    // Update total PnL
    f.total_pnl += pnl;
    
    // Update equity curve
    f.equity += pnl;

    // Track wins/losses
    if (pnl >= 0.0) {
        f.wins++;
    } else {
        f.losses++;
    }

    // Update max drawdown (most negative equity)
    if (f.equity < f.max_drawdown) {
        f.max_drawdown = f.equity;
    }

    // Update win rate
    int total = f.wins + f.losses;
    if (total > 0) {
        f.win_rate = static_cast<double>(f.wins) / 
                     static_cast<double>(total);
    }
}

bool StrategyFitnessEngine::isHealthy(
    const std::string& engine
) const {
    auto it = fitness.find(engine);
    if (it == fitness.end()) {
        // No history = assume healthy (allow new strategies)
        return true;
    }

    const FitnessStats& f = it->second;
    int total = f.wins + f.losses;
    
    // Need minimum trades for statistical significance
    if (total < 10) {
        return true;
    }

    // Check drawdown limit
    if (f.max_drawdown <= max_dd_limit) {
        return false;
    }

    // Check win rate
    if (f.win_rate < min_win_rate) {
        return false;
    }

    return true;
}

const FitnessStats&
StrategyFitnessEngine::stats(
    const std::string& engine
) const {
    static FitnessStats empty;
    auto it = fitness.find(engine);
    if (it == fitness.end()) return empty;
    return it->second;
}

}
