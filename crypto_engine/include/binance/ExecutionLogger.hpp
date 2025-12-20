#pragma once

#include "ExecutionSink.hpp"

namespace binance {

class ExecutionLogger : public ExecutionSink {
public:
    void on_intent(const ExecutionIntent& intent) override;
};

}
