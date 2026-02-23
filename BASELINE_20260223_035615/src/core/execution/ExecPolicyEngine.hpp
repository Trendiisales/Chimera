#pragma once
#include <string>
#include "../control/LatencyTypes.hpp"

namespace chimera {

enum class ExecPolicy {
    POST_ONLY = 0,
    HYBRID = 1,
    DISABLED = 2
};

class ExecPolicyEngine {
public:
    ExecPolicyEngine();

    void update(LatencyState s);
    ExecPolicy policy() const;
    std::string policy_string() const;

private:
    ExecPolicy m_policy;
};

}
