#pragma once
#include "CapitalSink.hpp"
#include <iostream>

namespace chimera {

class CapitalSinkStdout final : public CapitalSink {
public:
    void publish(const CapitalState& s) override;
};

}
