#include "ExecPolicySinkStdout.hpp"

namespace chimera {

void ExecPolicySinkStdout::publish(const ExecPolicyState& s) {
    std::cout
        << "[EXEC_POLICY]"
        << " enabled=" << (s.trading_enabled ? 1 : 0)
        << " mode=" << static_cast<int>(s.mode)
        << " size_mult=" << s.size_multiplier
        << " hard_kill=" << (s.hard_kill ? 1 : 0)
        << "\n";
}

}
