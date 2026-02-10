#pragma once
#include <string>
#include <cmath>

class DrawdownGate {
public:
    static bool allowTrade(
        const std::string& symbol,
        double account_equity,
        double current_dd,
        double stop_distance,
        double lot_size
    ) {
        const double max_dd_pct = 0.05;
        double symbol_risk = (symbol == "XAUUSD") ? 1.0 : 0.7;
        double trade_risk = stop_distance * lot_size * symbol_risk;
        double remaining_dd = account_equity * max_dd_pct - current_dd;
        
        if (trade_risk > remaining_dd * 0.25) {
            return false;
        }
        
        return true;
    }
};
