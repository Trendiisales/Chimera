#pragma once

#include <atomic>
#include <chrono>

namespace chimera {

class RateLimitGovernor {
public:
    void onResponse(
        int used_weight,
        int limit
    );

    bool allow() const;

private:
    std::atomic<int> used{0};
    std::atomic<int> max{1200};

    std::chrono::steady_clock::time_point window =
        std::chrono::steady_clock::now();
};

}
