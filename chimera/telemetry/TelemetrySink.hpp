#pragma once
#include "TelemetryEvent.hpp"

namespace chimera {

class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;
    virtual void publish(const TelemetryEvent& ev) = 0;
};

}
