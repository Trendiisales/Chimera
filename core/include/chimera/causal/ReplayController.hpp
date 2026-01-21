#pragma once

#include <string>
#include <vector>
#include <functional>
#include "chimera/causal/ReplayMode.hpp"
#include "chimera/causal/ShadowExecutor.hpp"
#include "chimera/causal/SignalAttributionLedger.hpp"
#include "chimera/causal/CounterfactualEngine.hpp"

namespace chimera {

// Orchestrates the entire causal testing workflow
class ReplayController {
public:
    ReplayController(
        ReplayMode& replay,
        CounterfactualEngine& counterfactual
    );

    // Step 1: Configure deterministic replay
    void configureDeterministicReplay(
        int64_t start_timestamp_ms,
        int64_t end_timestamp_ms
    );

    // Step 2: Load historical data for replay
    using DataLoader = std::function<void()>;
    void setDataLoader(DataLoader loader);

    // Step 3: Run baseline (all signals enabled)
    void runBaselineExperiment(
        ShadowExecutor& shadow,
        SignalAttributionLedger& ledger
    );

    // Step 4: Run counterfactual experiments (one signal disabled each)
    using CounterfactualLedgers = std::unordered_map<std::string, SignalAttributionLedger>;
    CounterfactualLedgers runCounterfactualExperiments(
        ShadowExecutor& shadow
    );

    // Step 5: Analyze and report results
    struct CausalReport {
        double baseline_total_pnl_bps;
        int baseline_trade_count;
        std::vector<CounterfactualResult> signal_contributions;
    };
    
    CausalReport generateCausalReport(
        const SignalAttributionLedger& baseline_ledger,
        const CounterfactualLedgers& cf_ledgers
    );

    // Save report to disk
    void saveReport(
        const CausalReport& report,
        const std::string& filepath
    ) const;

private:
    ReplayMode& replay_mode_;
    CounterfactualEngine& counterfactual_;
    
    int64_t start_ts_;
    int64_t end_ts_;
    DataLoader data_loader_;
};

}
