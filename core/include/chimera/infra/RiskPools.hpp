#pragma once

#include <string>
#include <unordered_map>

namespace chimera {

struct RiskPool {
    double max_exposure = 0.0;
    double current = 0.0;
};

class RiskPools {
public:
    void set(
        const std::string& symbol,
        double max_exp
    );

    bool allow(
        const std::string& symbol,
        double qty
    );

    void onFill(
        const std::string& symbol,
        double delta
    );

private:
    std::unordered_map<std::string, RiskPool> pools;
};

}
