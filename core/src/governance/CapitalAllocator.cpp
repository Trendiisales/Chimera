#include "chimera/governance/CapitalAllocator.hpp"
#include <cmath>
#include <numeric>

namespace chimera {

CapitalAllocator::CapitalAllocator(
    StrategyFitnessEngine& f,
    CorrelationGovernor& c,
    RegimeClassifier& r
) : fitness_engine(f),
    corr_governor(c),
    regime_classifier(r) {}

void CapitalAllocator::setBaseCapital(
    double cap
) {
    base_capital = cap;
}

void CapitalAllocator::setMinWeight(
    double w
) {
    min_weight = w;
}

void CapitalAllocator::setMaxWeight(
    double w
) {
    max_weight = w;
}

void CapitalAllocator::registerEngine(
    const std::string& engine
) {
    std::lock_guard<std::mutex> lock(mtx);
    alloc[engine] = {};
    pnl_history[engine] = {};
}

void CapitalAllocator::onFill(
    const std::string& engine,
    double pnl
) {
    std::lock_guard<std::mutex> lock(mtx);
    auto& hist = pnl_history[engine];
    hist.push_back(pnl);
    if (hist.size() > 100) {
        hist.erase(hist.begin());
    }
}

double CapitalAllocator::computeVolatility(
    const std::string& engine
) const {
    auto it = pnl_history.find(engine);
    if (it == pnl_history.end()) return 1.0;

    const auto& v = it->second;
    if (v.size() < 5) return 1.0;

    double mean =
        std::accumulate(
            v.begin(),
            v.end(),
            0.0
        ) / v.size();

    double var = 0.0;
    for (double x : v) {
        double d = x - mean;
        var += d * d;
    }

    return std::sqrt(
        var / v.size()
    ) + 1e-9;
}

double CapitalAllocator::computeScore(
    const std::string& engine
) {
    const FitnessStats& f =
        fitness_engine.stats(engine);

    double vol =
        computeVolatility(engine);

    double sharpe_like =
        f.total_pnl / vol;

    double dd_penalty =
        std::abs(f.max_drawdown);

    double corr_penalty =
        corr_governor.allowTrade(engine)
            ? 0.0
            : 1.0;

    double regime_bonus =
        regime_classifier
            .quality() *
        0.5;

    double score =
        sharpe_like +
        regime_bonus -
        dd_penalty -
        corr_penalty;

    AllocationStats& a =
        alloc[engine];

    a.score = score;
    a.sharpe_like =
        sharpe_like;
    a.drawdown =
        f.max_drawdown;
    a.correlation_penalty =
        corr_penalty;

    return score;
}

void CapitalAllocator::rebalance() {
    std::lock_guard<std::mutex> lock(mtx);

    double total_score = 0.0;

    for (auto& kv : alloc) {
        double s =
            computeScore(
                kv.first
            );
        if (s > 0.0) {
            total_score += s;
        }
    }

    for (auto& kv : alloc) {
        AllocationStats& a =
            kv.second;

        if (!fitness_engine
                .isHealthy(
                    kv.first
                )) {
            a.enabled = false;
            a.weight = 0.0;
            continue;
        }

        if (total_score <= 0.0) {
            a.weight = min_weight;
            continue;
        }

        double w =
            a.score /
            total_score;

        if (w < min_weight)
            w = min_weight;
        if (w > max_weight)
            w = max_weight;

        a.weight = w;
        a.enabled = true;
    }
}

double CapitalAllocator::capitalFor(
    const std::string& engine
) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = alloc.find(engine);
    if (it == alloc.end()) {
        return 0.0;
    }
    return base_capital *
           it->second.weight;
}

const AllocationStats&
CapitalAllocator::stats(
    const std::string& engine
) const {
    static AllocationStats empty;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = alloc.find(engine);
    if (it == alloc.end()) {
        return empty;
    }
    return it->second;
}

}
