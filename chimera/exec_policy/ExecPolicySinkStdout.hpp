#pragma once
#include "ExecPolicySink.hpp"
#include <iostream>

namespace chimera {

class ExecPolicySinkStdout final : public ExecPolicySink {
public:
    void publish(const ExecPolicyState& state) override;
};

}
