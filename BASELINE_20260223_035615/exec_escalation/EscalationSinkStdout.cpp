#include "EscalationSinkStdout.hpp"

namespace chimera {

void EscalationSinkStdout::publish(const EscalationDecision& d) {
    std::cout
        << "[ESCALATION]"
        << " id=" << d.causal_id
        << " action=" << static_cast<int>(d.action)
        << " conf=" << d.confidence
        << "\n";
}

}
