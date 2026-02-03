#pragma once
#include <cstdint>
#include <array>

namespace chimera {

static constexpr int DRIFT_WINDOW = 20;

class DriftFilter {
public:
    DriftFilter();

    void set_multiplier(double m);

    bool is_trending(double mid_now,
                     double mid_prev,
                     double spread) const;

private:
    double m_mult;
};

}
