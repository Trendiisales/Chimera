#pragma once

#include "chimera/execution/ExecutionEngine.hpp"
#include "chimera/strategy/Microstructure.hpp"

namespace chimera {

class ETHFade {
public:
    ETHFade(
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

    double max_spread = 0.05;
    double fade_ofi_threshold = 5.0;
    double order_size = 0.01;
};

}
