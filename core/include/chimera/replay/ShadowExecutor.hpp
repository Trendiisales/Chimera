#pragma once
#include <string>
#include <functional>

namespace chimera::replay {

class ShadowExecutor {
public:
    using DecisionFn = std::function<bool(const std::string& engine)>;

    void setGate(DecisionFn fn) {
        gate = fn;
    }

    bool allow(const std::string& engine) const {
        if (!gate) return true;
        return gate(engine);
    }

private:
    DecisionFn gate;
};

}
