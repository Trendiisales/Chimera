#pragma once
#include "CapitalState.hpp"

namespace chimera {

class CapitalSink {
public:
    virtual ~CapitalSink() = default;
    virtual void publish(const CapitalState& state) = 0;
};

}
