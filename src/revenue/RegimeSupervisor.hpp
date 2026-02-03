#pragma once
#include <unordered_map>
#include <string>
#include <mutex>

namespace chimera {

// ---------------------------------------------------------------------------
// RegimeSupervisor — Adaptive engine rotation based on market conditions
//
// Prevents engines from trading in regimes where they lose money:
// - QPMM disabled in trends (would bleed money making markets)
// - MeanReversion disabled in high volatility (false mean reversions)
// - Engines run only when market conditions favor their strategy type
//
// This is DEFENSIVE revenue: preventing losses = adding edge
// ---------------------------------------------------------------------------

enum class MarketRegime {
    RANGING,    // Oscillating, low vol - good for QPMM, MeanRev
    TRENDING,   // Directional move - good for momentum, bad for MM
    HIGH_VOL    // Volatile, wide spreads - risky for mean reversion
};

class RegimeSupervisor {
public:
    RegimeSupervisor(double vol_threshold = 0.003,   // 30bps volatility
                     double trend_threshold = 0.0007); // 7bps slope
    
    // Update regime classification for a symbol based on current market data
    // volatility = realized vol (e.g. std dev of returns over recent window)
    // slope = trend slope in basis points (EMA derivative)
    void update(const std::string& symbol, double volatility, double slope_bps);
    
    // Check if engine is allowed to trade this symbol in current regime
    // Returns false if regime is unfavorable for this engine type
    bool allow_engine(const std::string& symbol, 
                     const std::string& engine_id) const;
    
    // Get current regime for logging/diagnostics
    MarketRegime get_regime(const std::string& symbol) const;
    
    // Get regime name for logging
    static const char* regime_name(MarketRegime r);

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, MarketRegime> regimes_;
    
    double vol_threshold_;    // Volatility threshold for HIGH_VOL
    double trend_threshold_;  // Slope threshold for TRENDING
};

} // namespace chimera
