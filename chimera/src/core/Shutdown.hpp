#pragma once
#include <atomic>

namespace chimera {

inline std::atomic<bool>& running_flag() {
    static std::atomic<bool> running{true};
    return running;
}

inline void request_shutdown() {
    running_flag().store(false);
}

inline bool is_running() {
    return running_flag().load();
}

}
