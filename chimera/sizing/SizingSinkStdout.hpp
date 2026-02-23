#pragma once
#include "SizingSink.hpp"
#include <iostream>

namespace chimera {

class SizingSinkStdout final : public SizingSink {
public:
    void publish(const SizingDecision& d) override;
};

}
