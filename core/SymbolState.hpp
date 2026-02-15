#pragma once
#include <cstdint>
#include <string>

namespace ChimeraV2 {

struct SymbolState {
    std::string symbol;

    double mid = 0.0;
    double spread = 0.0;
    double velocity = 0.0;

    double short_vol = 0.0;
    double long_vol = 0.0;
    double compression_ratio = 0.0;
    double acceleration = 0.0;
    double structural_momentum = 0.0;

    uint64_t timestamp_ns = 0;
};

}
