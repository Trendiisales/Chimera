#pragma once
#include "EscalationSink.hpp"
#include <iostream>

namespace chimera {

class EscalationSinkStdout final : public EscalationSink {
public:
    void publish(const EscalationDecision& d) override;
};

}
