#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace chimera {

// Signal configuration for shadow execution
struct SignalConfig {
    std::string name;
    bool enabled;
};

// Hypothetical fill for shadow strategies
struct HypotheticalFill {
    std::string trade_id;
    std::string engine;
    std::string symbol;
    double qty;
    double price;
    bool is_buy;
    double pnl;
    
    // Signal states at decision time
    std::unordered_map<std::string, double> signal_values;
};

// Shadow execution runs N strategy variants on same data
// Only ONE actually trades, others log hypotheticals
class ShadowExecutor {
public:
    // Configure which signals are enabled for this shadow run
    void configureSignals(const std::vector<SignalConfig>& configs);
    
    // Check if a signal is enabled in current configuration
    bool isSignalEnabled(const std::string& signal) const;
    
    // Record signal value at decision time
    void recordSignalValue(
        const std::string& trade_id,
        const std::string& signal,
        double value
    );
    
    // Record hypothetical trade outcome
    void recordHypotheticalFill(const HypotheticalFill& fill);
    
    // Get all hypothetical fills for analysis
    const std::vector<HypotheticalFill>& getHypotheticalFills() const;
    
    // Clear all recorded data (for next run)
    void clear();
    
    // Get signal configuration
    std::vector<SignalConfig> getSignalConfig() const;

private:
    std::unordered_map<std::string, bool> signal_enabled_;
    std::vector<HypotheticalFill> hypothetical_fills_;
    
    // Temporary storage for building fills
    std::unordered_map<std::string, 
        std::unordered_map<std::string, double>> pending_signals_;
};

}
