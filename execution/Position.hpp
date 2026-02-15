#pragma once
#include <string>
#include <cstdint>
#include "../supervision/V2Proposal.hpp"

namespace ChimeraV2 {

struct Position {
    std::string symbol;
    int engine_id;

    Side side;
    double entry_price;
    double stop_price;
    double target_price;
    double size;

    uint64_t entry_time_ns;
    uint64_t max_hold_ns;

    bool open = true;
};

}
