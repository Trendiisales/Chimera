#include "revenue/RegimeSupervisor.hpp"
#include <cmath>
#include <iostream>

namespace chimera {

// Normalize symbol strings to handle variations like ETHUSDT vs ETH/USDT
static std::string normalize_symbol(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '/' && c != '-' && c != '_') {
            out.push_back(c);
        }
    }
    return out;
}

RegimeSupervisor::RegimeSupervisor(double vol_th, double trend_th)
    : vol_threshold_(vol_th), trend_threshold_(trend_th) {}

void RegimeSupervisor::update(const std::string& symbol, 
                              double volatility, 
                              double slope_bps) {
    MarketRegime regime = MarketRegime::RANGING;
    
    // Classify regime based on volatility and trend strength
    if (volatility > vol_threshold_) {
        regime = MarketRegime::HIGH_VOL;
    } else if (std::fabs(slope_bps) > trend_threshold_) {
        regime = MarketRegime::TRENDING;
    }
    
    std::string norm_sym = normalize_symbol(symbol);
    
    std::lock_guard<std::mutex> lock(mtx_);
    
    // Log regime changes
    auto it = regimes_.find(norm_sym);
    if (it == regimes_.end() || it->second != regime) {
        std::cout << "[REGIME] " << symbol << " " 
                  << regime_name(it != regimes_.end() ? it->second : MarketRegime::RANGING)
                  << " → " << regime_name(regime) 
                  << " (vol=" << volatility << " slope=" << slope_bps << "bps)\n";
    }
    
    regimes_[norm_sym] = regime;
}

bool RegimeSupervisor::allow_engine(const std::string& symbol,
                                    const std::string& engine_id) const {
    std::string norm_sym = normalize_symbol(symbol);
    
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = regimes_.find(norm_sym);
    if (it == regimes_.end()) {
        return true;  // No regime data yet, allow trading
    }
    
    MarketRegime regime = it->second;
    
    // ---------------------------------------------------------------------------
    // Engine-specific regime filters
    // ---------------------------------------------------------------------------
    
    // QPMM (Queue Market Maker) - stop in trends
    // Market making loses money when price is trending strongly
    if (regime == MarketRegime::TRENDING && engine_id == "QPMM") {
        return false;
    }
    
    // MeanReversion - stop in high volatility
    // Mean reversion assumes stable mean, breaks down in volatile periods
    if (regime == MarketRegime::HIGH_VOL && engine_id == "MEAN_REV") {
        return false;
    }
    
    // ImpulseReversion - reduce activity in high vol
    // Impulse fade works best in lower vol environments
    if (regime == MarketRegime::HIGH_VOL && engine_id == "IMPULSE_REV") {
        return false;
    }
    
    return true;
}

MarketRegime RegimeSupervisor::get_regime(const std::string& symbol) const {
    std::string norm_sym = normalize_symbol(symbol);
    
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = regimes_.find(norm_sym);
    if (it == regimes_.end()) {
        return MarketRegime::RANGING;  // Default
    }
    
    return it->second;
}

const char* RegimeSupervisor::regime_name(MarketRegime r) {
    switch (r) {
        case MarketRegime::RANGING:   return "RANGING";
        case MarketRegime::TRENDING:  return "TRENDING";
        case MarketRegime::HIGH_VOL:  return "HIGH_VOL";
        default:                      return "UNKNOWN";
    }
}

} // namespace chimera
