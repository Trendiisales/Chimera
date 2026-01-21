#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "chimera/causal/ShadowExecutor.hpp"
#include "chimera/causal/SignalAttributionLedger.hpp"

namespace chimera {

// Result of a counterfactual experiment
struct CounterfactualResult {
    std::string disabled_signal;
    double baseline_pnl_bps;
    double counterfactual_pnl_bps;
    double delta_pnl_bps;  // counterfactual - baseline
    int baseline_trade_count;
    int counterfactual_trade_count;
    double win_rate_delta;
};

// Engine for running counterfactual experiments
// Disables one signal at a time and measures delta PnL
class CounterfactualEngine {
public:
    // List of signals that can be disabled
    void addSignal(const std::string& signal);
    
    // Run baseline (all signals enabled)
    void runBaseline(ShadowExecutor& shadow);
    
    // Run counterfactual (one signal disabled)
    void runCounterfactual(
        const std::string& disabled_signal,
        ShadowExecutor& shadow
    );
    
    // Compute causal contribution of each signal
    std::vector<CounterfactualResult> computeCausalContributions(
        const SignalAttributionLedger& baseline_ledger,
        const std::unordered_map<std::string, SignalAttributionLedger>& 
            counterfactual_ledgers
    ) const;
    
    // Get list of signals to test
    const std::vector<std::string>& getSignals() const;

private:
    double computePnL(const SignalAttributionLedger& ledger) const;
    double computeWinRate(const SignalAttributionLedger& ledger) const;

private:
    std::vector<std::string> signals_;
};

}
