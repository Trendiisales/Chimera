#include "chimera/causal/ReplayController.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <iostream>

namespace json = boost::json;

namespace chimera {

ReplayController::ReplayController(
    ReplayMode& replay,
    CounterfactualEngine& counterfactual
) : replay_mode_(replay),
    counterfactual_(counterfactual),
    start_ts_(0),
    end_ts_(0) {}

void ReplayController::configureDeterministicReplay(
    int64_t start_timestamp_ms,
    int64_t end_timestamp_ms
) {
    start_ts_ = start_timestamp_ms;
    end_ts_ = end_timestamp_ms;
    
    // Enable replay mode
    replay_mode_.enable();
    
    // Set frozen timestamp to start
    replay_mode_.setFrozenTimestamp(start_timestamp_ms);
    
    std::cout << "[ReplayController] Configured deterministic replay: "
              << start_timestamp_ms << " -> " << end_timestamp_ms << "\n";
}

void ReplayController::setDataLoader(DataLoader loader) {
    data_loader_ = loader;
}

void ReplayController::runBaselineExperiment(
    ShadowExecutor& shadow,
    SignalAttributionLedger& ledger
) {
    std::cout << "[ReplayController] Running baseline experiment (all signals enabled)...\n";
    
    // Clear previous data
    shadow.clear();
    ledger.clear();
    
    // Configure all signals enabled
    counterfactual_.runBaseline(shadow);
    
    // Load and replay data
    if (data_loader_) {
        data_loader_();
    }
    
    std::cout << "[ReplayController] Baseline complete: " 
              << ledger.getAttributions().size() << " trades\n";
}

ReplayController::CounterfactualLedgers 
ReplayController::runCounterfactualExperiments(
    ShadowExecutor& shadow
) {
    CounterfactualLedgers cf_ledgers;
    
    const auto& signals = counterfactual_.getSignals();
    
    std::cout << "[ReplayController] Running " << signals.size() 
              << " counterfactual experiments...\n";
    
    for (const auto& signal : signals) {
        std::cout << "[ReplayController]   Disabling signal: " << signal << "\n";
        
        // Clear previous run
        shadow.clear();
        
        // Configure with this signal disabled
        counterfactual_.runCounterfactual(signal, shadow);
        
        // Create ledger for this counterfactual
        SignalAttributionLedger ledger;
        
        // Load and replay data
        if (data_loader_) {
            data_loader_();
        }
        
        // Store results
        cf_ledgers[signal] = ledger;
        
        std::cout << "[ReplayController]     Result: " 
                  << ledger.getAttributions().size() << " trades\n";
    }
    
    return cf_ledgers;
}

ReplayController::CausalReport ReplayController::generateCausalReport(
    const SignalAttributionLedger& baseline_ledger,
    const CounterfactualLedgers& cf_ledgers
) {
    CausalReport report;
    
    // Baseline stats
    double total_pnl = 0.0;
    for (const auto& attr : baseline_ledger.getAttributions()) {
        total_pnl += attr.total_pnl_bps;
    }
    
    report.baseline_total_pnl_bps = total_pnl;
    report.baseline_trade_count = baseline_ledger.getAttributions().size();
    
    // Counterfactual contributions
    report.signal_contributions = 
        counterfactual_.computeCausalContributions(
            baseline_ledger,
            cf_ledgers
        );
    
    return report;
}

void ReplayController::saveReport(
    const CausalReport& report,
    const std::string& filepath
) const {
    json::object root;
    
    root["baseline_total_pnl_bps"] = report.baseline_total_pnl_bps;
    root["baseline_trade_count"] = report.baseline_trade_count;
    
    json::array contributions;
    for (const auto& res : report.signal_contributions) {
        json::object obj;
        obj["disabled_signal"] = res.disabled_signal;
        obj["baseline_pnl_bps"] = res.baseline_pnl_bps;
        obj["counterfactual_pnl_bps"] = res.counterfactual_pnl_bps;
        obj["delta_pnl_bps"] = res.delta_pnl_bps;
        obj["baseline_trade_count"] = res.baseline_trade_count;
        obj["counterfactual_trade_count"] = res.counterfactual_trade_count;
        obj["win_rate_delta"] = res.win_rate_delta;
        
        contributions.push_back(obj);
    }
    
    root["signal_contributions"] = contributions;
    
    std::ofstream out(filepath);
    out << json::serialize(root);
    
    std::cout << "[ReplayController] Causal report saved to: " << filepath << "\n";
}

}
