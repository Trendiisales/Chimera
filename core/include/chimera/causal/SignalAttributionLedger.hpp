#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

namespace chimera {

// Detailed attribution of PnL to individual signals
struct SignalAttribution {
    std::string trade_id;
    std::string engine;
    std::string symbol;
    int64_t timestamp_ms;
    
    // Signal contributions (basis points)
    double ofi_contrib_bps = 0.0;
    double impulse_contrib_bps = 0.0;
    double spread_contrib_bps = 0.0;
    double depth_contrib_bps = 0.0;
    double toxic_contrib_bps = 0.0;
    double vpin_contrib_bps = 0.0;
    double regime_contrib_bps = 0.0;
    double funding_contrib_bps = 0.0;
    
    // Execution quality
    double execution_slippage_bps = 0.0;
    double fee_drag_bps = 0.0;
    
    // Total
    double total_pnl_bps = 0.0;
    
    // Raw signal values at decision time
    std::unordered_map<std::string, double> signal_values;
};

// Ledger for tracking signal-level PnL attribution
class SignalAttributionLedger {
public:
    // Record a trade with full signal attribution
    void recordTrade(const SignalAttribution& attr);
    
    // Get all recorded attributions
    const std::vector<SignalAttribution>& getAttributions() const;
    
    // Compute aggregate statistics per signal
    struct SignalStats {
        double total_contrib_bps = 0.0;
        double mean_contrib_bps = 0.0;
        double positive_contrib_bps = 0.0;
        double negative_contrib_bps = 0.0;
        int trade_count = 0;
        int positive_count = 0;
        int negative_count = 0;
    };
    
    std::unordered_map<std::string, SignalStats> computeSignalStats() const;
    
    // Save ledger to disk for analysis
    void saveToDisk(const std::string& filepath) const;
    
    // Load ledger from disk
    void loadFromDisk(const std::string& filepath);
    
    // Clear all records
    void clear();

private:
    std::vector<SignalAttribution> attributions_;
};

}
