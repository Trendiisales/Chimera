#include "chimera/infra/PredictiveRouter.hpp"

namespace chimera {

PredictiveRouter::PredictiveRouter(
    LatencyEngine& lat,
    VenueBiasEngine& bias
) : latency(lat),
    bias_engine(bias) {}

VenueQuote PredictiveRouter::select(
    const VenueQuote& a,
    const VenueQuote& b
) const {
    LatencyStats la =
        latency.stats(a.venue);
    LatencyStats lb =
        latency.stats(b.venue);

    double score_a =
        (a.ask - a.bid) +
        a.fee_bps +
        la.rest_rtt_ms * 0.01 +
        la.ws_lag_ms * 0.01 +
        bias_engine.bias(a.venue);

    double score_b =
        (b.ask - b.bid) +
        b.fee_bps +
        lb.rest_rtt_ms * 0.01 +
        lb.ws_lag_ms * 0.01 +
        bias_engine.bias(b.venue);

    return score_a < score_b
        ? a
        : b;
}

}
