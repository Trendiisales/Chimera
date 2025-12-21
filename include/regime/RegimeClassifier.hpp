#pragma once
#include "signal/SignalTypes.hpp"
#include "regime/RegimeTypes.hpp"

namespace Chimera {

class RegimeClassifier {
public:
    RegimeClassifier();

    RegimeState on_signal(const AggregatedSignal& sig);

private:
    double vol_low_;
    double vol_high_;

    double trend_thresh_;
    double mean_rev_thresh_;

    double last_composite_;
};

}
