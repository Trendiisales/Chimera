#pragma once

namespace chimera {

class RegimeClassifier {
public:
    enum class Regime {
        BALANCED,
        CHAOTIC,
        DEAD
    };

    RegimeClassifier();

    void update(double spread_bps, double ofi_accel, bool impulse_open, int tick_rate);

    Regime current() const;
    
    // For CapitalAllocator integration
    double quality() const;

private:
    Regime regime_;
    double quality_score_;
};

}
