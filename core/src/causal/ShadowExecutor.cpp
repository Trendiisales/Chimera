#include "chimera/causal/ShadowExecutor.hpp"

namespace chimera {

void ShadowExecutor::configureSignals(
    const std::vector<SignalConfig>& configs
) {
    signal_enabled_.clear();
    for (const auto& cfg : configs) {
        signal_enabled_[cfg.name] = cfg.enabled;
    }
}

bool ShadowExecutor::isSignalEnabled(const std::string& signal) const {
    auto it = signal_enabled_.find(signal);
    if (it == signal_enabled_.end()) {
        return true;  // Default: enabled if not specified
    }
    return it->second;
}

void ShadowExecutor::recordSignalValue(
    const std::string& trade_id,
    const std::string& signal,
    double value
) {
    pending_signals_[trade_id][signal] = value;
}

void ShadowExecutor::recordHypotheticalFill(const HypotheticalFill& fill) {
    HypotheticalFill complete_fill = fill;
    
    // Attach recorded signal values
    auto it = pending_signals_.find(fill.trade_id);
    if (it != pending_signals_.end()) {
        complete_fill.signal_values = it->second;
    }
    
    hypothetical_fills_.push_back(complete_fill);
}

const std::vector<HypotheticalFill>& 
ShadowExecutor::getHypotheticalFills() const {
    return hypothetical_fills_;
}

void ShadowExecutor::clear() {
    hypothetical_fills_.clear();
    pending_signals_.clear();
}

std::vector<SignalConfig> ShadowExecutor::getSignalConfig() const {
    std::vector<SignalConfig> configs;
    for (const auto& kv : signal_enabled_) {
        configs.push_back({kv.first, kv.second});
    }
    return configs;
}

}
