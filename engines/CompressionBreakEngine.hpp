#pragma once
#include "IEngine.hpp"
#include "../config/V2Config.hpp"
#include <cmath>

namespace ChimeraV2 {

class CompressionBreakEngine : public IEngine {
public:
    int id() const override { return 2; }
    const char* name() const override { return "CompressionBreak"; }

    V2Proposal evaluate(const SymbolState& s) override {
        V2Proposal p;
        p.engine_id = id();
        p.symbol = s.symbol;
        p.timestamp_ns = s.timestamp_ns;

        if (s.compression_ratio > 0.6)
            return p;

        // V1 PROVEN: Minimum drift velocity
        if (std::abs(s.velocity) < V2Config::MIN_DRIFT_VELOCITY)
            return p;

        p.valid = true;
        p.side = (s.velocity > 0.0) ? Side::BUY : Side::SELL;
        p.size = 1.0;
        p.structural_score = 0.8;
        p.confidence = 0.55;
        
        // V1 PROVEN: Use fixed stop distance per symbol
        p.estimated_risk = (s.symbol == "XAUUSD") 
            ? V2Config::XAU_STOP_DISTANCE 
            : V2Config::XAG_STOP_DISTANCE;

        return p;
    }

    void on_trade_closed(double pnl, double R) override {
        // Reserved for future internal tracking
        (void)pnl;
        (void)R;
    }
};

}
