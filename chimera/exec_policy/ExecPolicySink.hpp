#pragma once
#include "ExecPolicyState.hpp"

namespace chimera {

class ExecPolicySink {
public:
    virtual ~ExecPolicySink() = default;
    virtual void publish(const ExecPolicyState& state) = 0;
};

}
