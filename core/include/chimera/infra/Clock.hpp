#pragma once

#include <chrono>
#include <type_traits>
#include <cstdint>

namespace chimera::infra {

using MonoClock = std::chrono::steady_clock;
using MonoTime  = MonoClock::time_point;
using MonoDur   = MonoClock::duration;

inline MonoTime now() noexcept {
    return MonoClock::now();
}

// Convert MonoTime to nanoseconds since epoch
inline uint64_t to_ns(MonoTime t) noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t.time_since_epoch()
        ).count()
    );
}

template<typename T>
struct forbid_system_clock {
    static_assert(!std::is_same_v<T, std::chrono::system_clock>,
        "FATAL: system_clock is forbidden. Use chimera::infra::MonoTime");
};

} // namespace chimera::infra
