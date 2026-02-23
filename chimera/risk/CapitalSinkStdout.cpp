#include "CapitalSinkStdout.hpp"

namespace chimera {

void CapitalSinkStdout::publish(const CapitalState& s) {
    std::cout
        << "[CAPITAL]"
        << " mode=" << static_cast<int>(s.mode)
        << " mult=" << s.global_multiplier
        << " dd=" << s.drawdown
        << "\n";
}

}
