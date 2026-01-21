#pragma once

#include <atomic>
#include <string>

namespace chimera {

class ClockSync {
public:
    explicit ClockSync(const std::string& rest_url);

    void refresh();
    int64_t offsetMs() const;
    int64_t nowMs() const;

private:
    std::string url;
    std::atomic<int64_t> offset{0};
};

}
