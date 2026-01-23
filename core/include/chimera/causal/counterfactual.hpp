#pragma once

#include "chimera/causal/replay.hpp"
#include "chimera/causal/events.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace chimera::causal {

struct CounterfactualResult {
    std::string experiment_name;
    double baseline_pnl;
    double counterfactual_pnl;
    double delta_pnl;
    double delta_pnl_pct;
    int baseline_trades;
    int counterfactual_trades;
    int delta_trades;
};

struct SignalContribution {
    size_t signal_idx;
    double marginal_pnl;
    double win_rate;
    int trade_count;
    double sharpe;
};

class CounterfactualEngine {
public:
    CounterfactualEngine();
    
    // Run counterfactual: what if signal N was zero?
    CounterfactualResult run_without_signal(
        const ReplayStream& stream, 
        size_t signal_idx
    );
    
    // Run counterfactual: what if we changed risk limit?
    CounterfactualResult run_with_risk_limit(
        const ReplayStream& stream,
        double new_max_pos
    );
    
    // Compute marginal contribution of each signal
    std::vector<SignalContribution> compute_signal_contributions(
        const ReplayStream& stream
    );
    
    // Analyze causal chain: tick -> decision -> risk -> order -> fill -> pnl
    void analyze_causal_chain(const ReplayStream& stream);
    
private:
    double extract_pnl(const ReplayStream& stream);
    double compute_pnl_without_signal(const ReplayStream& stream, size_t signal_idx);
    double compute_pnl_with_risk_limit(const ReplayStream& stream, double new_max_pos);
    
    // Track parent-child relationships
    std::unordered_map<event_id_t, std::vector<event_id_t>> parent_map;
};

}
