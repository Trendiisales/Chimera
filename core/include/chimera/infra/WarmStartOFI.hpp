#pragma once

#include <string>
#include <unordered_map>
#include <chrono>

namespace chimera {

class WarmStartOFI {
public:
    void seed(
        const std::string& symbol,
        double ofi
    );

    double get(
        const std::string& symbol
    );

private:
    struct State {
        double value = 0.0;
        std::chrono::steady_clock::time_point ts;
    };

    std::unordered_map<std::string, State> map;
};

}
