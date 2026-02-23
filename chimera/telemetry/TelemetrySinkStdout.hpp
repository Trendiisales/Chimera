#pragma once
#include "TelemetrySink.hpp"
#include <iostream>

namespace chimera {

class TelemetrySinkStdout final : public TelemetrySink {
public:
    void publish(const TelemetryEvent& ev) override;
};

}
