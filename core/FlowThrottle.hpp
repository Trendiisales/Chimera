#pragma once
#include <chrono>

class FlowThrottle {
public:
    static bool allow(int seconds) {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        if (!init_) {
            last_ = now;
            init_ = true;
            return true;
        }
        auto dt = std::chrono::duration_cast<std::chrono::seconds>(now - last_).count();
        if (dt >= seconds) {
            last_ = now;
            return true;
        }
        return false;
    }
private:
    inline static bool init_ = false;
    inline static std::chrono::steady_clock::time_point last_;
};
