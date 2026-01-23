#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>

namespace chimera::snapshot {

struct EMAState {
    double value;
};

class EMASnapshot {
public:
    void capture(const std::string& key, double v) {
        states[key] = { v };
    }

    double restore(const std::string& key, double def = 0.0) const {
        auto it = states.find(key);
        if (it == states.end()) return def;
        return it->second.value;
    }

private:
    std::unordered_map<std::string, EMAState> states;
};

}
