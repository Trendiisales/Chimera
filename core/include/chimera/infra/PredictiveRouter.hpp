#pragma once

#include "chimera/infra/LatencyEngine.hpp"
#include "chimera/infra/VenueBiasEngine.hpp"
#include "chimera/infra/CrossVenueRouter.hpp"

namespace chimera {

class PredictiveRouter {
public:
    PredictiveRouter(
        LatencyEngine& lat,
        VenueBiasEngine& bias
    );

    VenueQuote select(
        const VenueQuote& a,
        const VenueQuote& b
    ) const;

private:
    LatencyEngine& latency;
    VenueBiasEngine& bias_engine;
};

}
