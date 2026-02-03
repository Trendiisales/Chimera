#pragma once
#include <cstdint>

namespace chimera {

enum class VolRegime {
    DEAD = 0,       // No trading
    NORMAL = 1,     // Scout mode
    EXPANSION = 2   // Full size
};

class VolatilityRegime {
public:
    VolatilityRegime();

    void update(double mid_price);

    VolRegime regime() const;
    double sigma() const;
    bool warmed() const { return m_count >= 10; }

private:
    static constexpr int WINDOW = 64;

    double m_returns[WINDOW];
    int m_idx;
    int m_count;
    double m_last_mid;

    double compute_sigma() const;
};

}
