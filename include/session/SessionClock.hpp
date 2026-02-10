// =============================================================================
// SessionClock.hpp - v4.18.0 - SESSION TIME AWARENESS
// =============================================================================
// Simple session boundary checker.
// Set start/end nanosecond timestamps. active() returns whether now is within.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>

namespace Chimera {

class SessionClock {
public:
    void set(uint64_t startNs, uint64_t endNs) {
        start_ = startNs;
        end_   = endNs;
    }

    bool active(uint64_t now_ns) const {
        return now_ns >= start_ && now_ns <= end_;
    }

    uint64_t start() const { return start_; }
    uint64_t end()   const { return end_; }

    bool isSet() const { return end_ > 0; }

    void reset() {
        start_ = 0;
        end_   = 0;
    }

private:
    uint64_t start_ = 0;
    uint64_t end_   = 0;
};

} // namespace Chimera
