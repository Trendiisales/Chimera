// ============================================================================
// crypto_engine/include/signal/SignalAggregator.hpp
// ============================================================================
// Production-grade signal aggregation for Binance
// ============================================================================
#pragma once

#include "../micro/CentralMicroEngine.hpp"

struct SignalVector {
    double obi;
    double microprice;
    double tfi;
    double vol;
};

class SignalAggregator {
public:
    inline SignalVector aggregate(const MicroSnapshot& s) const {
        return {
            clamp(s.obi),
            s.microprice,
            clamp(s.trade_imbalance),
            clamp(s.vol_burst)
        };
    }

private:
    static inline double clamp(double x) {
        return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
    }
};
