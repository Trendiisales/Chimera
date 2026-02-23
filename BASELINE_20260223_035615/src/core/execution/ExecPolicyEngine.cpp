#include "ExecPolicyEngine.hpp"

namespace chimera {

ExecPolicyEngine::ExecPolicyEngine()
    : m_policy(ExecPolicy::POST_ONLY) {}

void ExecPolicyEngine::update(LatencyState s) {
    if (s == LatencyState::OK) {
        m_policy = ExecPolicy::POST_ONLY;
    } else if (s == LatencyState::DEGRADED) {
        m_policy = ExecPolicy::HYBRID;
    } else {
        m_policy = ExecPolicy::DISABLED;
    }
}

ExecPolicy ExecPolicyEngine::policy() const {
    return m_policy;
}

std::string ExecPolicyEngine::policy_string() const {
    if (m_policy == ExecPolicy::POST_ONLY) return "POST_ONLY";
    if (m_policy == ExecPolicy::HYBRID) return "HYBRID";
    return "DISABLED";
}

}
