#pragma once

#include "ExecutionTypes.hpp"

namespace binance {

class ExecutionSink {
public:
    virtual ~ExecutionSink() = default;
    virtual void on_intent(const ExecutionIntent& intent) = 0;
};

}
