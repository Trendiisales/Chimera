#pragma once
#include "LatencyRecord.hpp"

namespace chimera {

class LatencySink {
public:
    virtual ~LatencySink() = default;
    virtual void publish(const LatencyRecord& rec) = 0;
};

}
