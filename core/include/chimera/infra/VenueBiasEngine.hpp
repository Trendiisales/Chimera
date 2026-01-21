#pragma once

#include <string>
#include <unordered_map>

namespace chimera {

struct VenueBias {
    double score = 0.0;
};

class VenueBiasEngine {
public:
    void onFill(
        const std::string& venue,
        double expected_price,
        double fill_price,
        double fee_bps,
        double latency_ms
    );

    double bias(
        const std::string& venue
    ) const;

private:
    std::unordered_map<std::string, VenueBias> map;
};

}
