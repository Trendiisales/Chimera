#pragma once
#include "EscalationDecision.hpp"

namespace chimera {

class EscalationSink {
public:
    virtual ~EscalationSink() = default;
    virtual void publish(const EscalationDecision& d) = 0;
};

}
