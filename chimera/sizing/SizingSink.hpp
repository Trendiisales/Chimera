#pragma once
#include "SizingDecision.hpp"

namespace chimera {

class SizingSink {
public:
    virtual ~SizingSink() = default;
    virtual void publish(const SizingDecision& d) = 0;
};

}
