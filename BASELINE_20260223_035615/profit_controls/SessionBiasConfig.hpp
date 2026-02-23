#pragma once
#include <cstdint>

namespace chimera {

struct SessionBiasConfig {
    uint64_t bucket_ns = 3'600'000'000ULL;

    double good_threshold = 0.6;
    double bad_threshold = 0.4;

    double max_upscale = 1.4;
    double max_downscale = 0.6;
};

}
