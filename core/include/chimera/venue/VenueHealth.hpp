#pragma once
#include <unordered_map>
#include <string>
#include <chrono>

namespace chimera::venue {

class VenueHealth {
public:
    using Clock = std::chrono::steady_clock;

    void beat(const std::string& venue) {
        last[venue] = Clock::now();
    }

    bool healthy(const std::string& venue, uint64_t max_ms) const {
        auto it = last.find(venue);
        if (it == last.end()) return false;
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - it->second
        ).count();
        return delta <= (long long)max_ms;
    }

private:
    std::unordered_map<std::string, Clock::time_point> last;
};

}
