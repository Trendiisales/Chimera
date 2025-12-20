#pragma once
#include "micro/MicroSignal.hpp"

namespace Chimera {

class Microprice {
public:
    Microprice();

    MicroSignal on_book(double bid_px, double ask_px,
                        double bid_qty, double ask_qty,
                        uint64_t ts_ns);

private:
    double last_;
};

}
