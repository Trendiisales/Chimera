#pragma once
#include "alerts/AlertTypes.hpp"
#include <atomic>

namespace Chimera {

class AlertBus {
public:
    AlertBus();

    void emit(AlertCode code, AlertLevel level);
    bool critical_active() const;

private:
    std::atomic<bool> critical_;
};

AlertBus& alert_bus();

}
