#pragma once
#include <atomic>

namespace binance {

class RuntimeControl {
    std::atomic<bool> running{true};

public:
    void stop() { running.store(false); }
    bool is_running() const { return running.load(); }
};

}
