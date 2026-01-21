#pragma once

#include "chimera/execution/ExecutionEngine.hpp"
#include "chimera/strategy/Microstructure.hpp"

namespace chimera {

class BTCCascade {
public:
    BTCCascade(
        ExecutionEngine& exec,
        Microstructure& micro
    );

    void onTick(
        const std::string& symbol,
        double bid,
        double ask,
        double spread,
        uint64_t ts_ns
    );

private:
    ExecutionEngine& execution;
    Microstructure& microstructure;

    double max_spread = 0.08;
    double momentum_ofi_threshold = 10.0;
    double order_size = 0.01;
};

}
