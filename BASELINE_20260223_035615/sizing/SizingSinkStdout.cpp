#include "SizingSinkStdout.hpp"

namespace chimera {

void SizingSinkStdout::publish(const SizingDecision& d) {
    std::cout
        << "[SIZING]"
        << " id=" << d.causal_id
        << " action=" << static_cast<int>(d.action)
        << " size=" << d.final_size
        << " conf=" << d.confidence
        << "\n";
}

}
