#include "chimera/causal/CounterfactualEngine.hpp"

namespace chimera {

void CounterfactualEngine::addSignal(const std::string& signal) {
    signals_.push_back(signal);
}

void CounterfactualEngine::runBaseline(ShadowExecutor& shadow) {
    // All signals enabled
    std::vector<SignalConfig> configs;
    for (const auto& sig : signals_) {
        configs.push_back({sig, true});
    }
    shadow.configureSignals(configs);
}

void CounterfactualEngine::runCounterfactual(
    const std::string& disabled_signal,
    ShadowExecutor& shadow
) {
    // Disable one signal, enable all others
    std::vector<SignalConfig> configs;
    for (const auto& sig : signals_) {
        configs.push_back({sig, sig != disabled_signal});
    }
    shadow.configureSignals(configs);
}

std::vector<CounterfactualResult> 
CounterfactualEngine::computeCausalContributions(
    const SignalAttributionLedger& baseline_ledger,
    const std::unordered_map<std::string, SignalAttributionLedger>& 
        counterfactual_ledgers
) const {
    std::vector<CounterfactualResult> results;
    
    double baseline_pnl = computePnL(baseline_ledger);
    double baseline_wr = computeWinRate(baseline_ledger);
    int baseline_count = baseline_ledger.getAttributions().size();
    
    for (const auto& kv : counterfactual_ledgers) {
        const std::string& disabled_signal = kv.first;
        const SignalAttributionLedger& cf_ledger = kv.second;
        
        double cf_pnl = computePnL(cf_ledger);
        double cf_wr = computeWinRate(cf_ledger);
        int cf_count = cf_ledger.getAttributions().size();
        
        CounterfactualResult res;
        res.disabled_signal = disabled_signal;
        res.baseline_pnl_bps = baseline_pnl;
        res.counterfactual_pnl_bps = cf_pnl;
        res.delta_pnl_bps = cf_pnl - baseline_pnl;
        res.baseline_trade_count = baseline_count;
        res.counterfactual_trade_count = cf_count;
        res.win_rate_delta = cf_wr - baseline_wr;
        
        results.push_back(res);
    }
    
    return results;
}

const std::vector<std::string>& CounterfactualEngine::getSignals() const {
    return signals_;
}

double CounterfactualEngine::computePnL(
    const SignalAttributionLedger& ledger
) const {
    double total = 0.0;
    for (const auto& attr : ledger.getAttributions()) {
        total += attr.total_pnl_bps;
    }
    return total;
}

double CounterfactualEngine::computeWinRate(
    const SignalAttributionLedger& ledger
) const {
    int wins = 0;
    int total = 0;
    
    for (const auto& attr : ledger.getAttributions()) {
        total++;
        if (attr.total_pnl_bps > 0) {
            wins++;
        }
    }
    
    if (total == 0) return 0.0;
    return static_cast<double>(wins) / total;
}

}
