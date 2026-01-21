#pragma once

#include <string>
#include <unordered_map>

namespace chimera {

struct FitnessStats {
    double total_pnl = 0.0;
    int wins = 0;
    int losses = 0;
    double max_drawdown = 0.0;
    double equity = 0.0;
    double win_rate = 0.0;
};

class StrategyFitnessEngine {
public:
    StrategyFitnessEngine();

    void recordTrade(
        const std::string& engine,
        double pnl
    );

    bool isHealthy(
        const std::string& engine
    ) const;

    const FitnessStats& stats(
        const std::string& engine
    ) const;

    void setDrawdownLimit(double dd);
    void setMinWinRate(double wr);

private:
    std::unordered_map<
        std::string,
        FitnessStats
    > fitness;

    double max_dd_limit = -0.02;
    double min_win_rate = 0.45;
};

}
