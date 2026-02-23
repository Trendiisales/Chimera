#pragma once
#include <iostream>
#include "ProfitControlSink.hpp"

namespace chimera {

class ProfitControlSinkStdout final : public ProfitControlSink {
public:
    void notify_pause() {
        std::cout << "[LOSS_SHUTDOWN] trading paused\n";
    }
};

}
