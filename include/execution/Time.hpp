#pragma once
#include <chrono>
#include <cstdint>

inline uint64_t monotonic_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

inline uint64_t safe_age_ms(uint64_t now_ms, uint64_t signal_ts_ms) {
    if (signal_ts_ms == 0) return UINT64_MAX;
    if (signal_ts_ms > now_ms) return 0;  // Clamp future signals
    return now_ms - signal_ts_ms;
}
