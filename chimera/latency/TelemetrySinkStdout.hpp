#pragma once
#include "LatencySink.hpp"
#include <iostream>

namespace chimera {

class TelemetrySinkStdout final : public LatencySink {
public:
    void publish(const LatencyRecord& rec) override;
};

}
