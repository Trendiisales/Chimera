#pragma once

#include <string>
#include <functional>

namespace chimera {

struct VenueQuote {
    std::string venue;
    double bid = 0.0;
    double ask = 0.0;
    double latency_ms = 0.0;
    double fee_bps = 0.0;
};

class CrossVenueRouter {
public:
    VenueQuote selectBest(
        const VenueQuote& a,
        const VenueQuote& b
    ) const;
};

}
