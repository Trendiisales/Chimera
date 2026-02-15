#pragma once
#include <string>
#include <cstdint>

namespace ChimeraV2 {

enum class Side {
    NONE,
    BUY,
    SELL
};

struct V2Proposal {
    int engine_id = -1;
    std::string symbol;
    bool valid = false;

    Side side = Side::NONE;
    double size = 0.0;

    double structural_score = 0.0;
    double confidence = 0.0;
    double estimated_risk = 0.0;

    uint64_t timestamp_ns = 0;
};

}
