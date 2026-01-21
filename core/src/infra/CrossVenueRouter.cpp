#include "chimera/infra/CrossVenueRouter.hpp"

namespace chimera {

VenueQuote CrossVenueRouter::selectBest(
    const VenueQuote& a,
    const VenueQuote& b
) const {
    double score_a =
        a.ask - a.bid +
        (a.latency_ms * 0.01) +
        a.fee_bps;

    double score_b =
        b.ask - b.bid +
        (b.latency_ms * 0.01) +
        b.fee_bps;

    return score_a < score_b
           ? a
           : b;
}

}
