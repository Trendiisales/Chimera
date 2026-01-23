#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>
#include <functional>

namespace chimera::watchdog {

class Watchdog {
public:
    using Clock = std::chrono::steady_clock;

    Watchdog(uint64_t max_idle_ms, std::function<void()> on_trigger)
        : max_idle(max_idle_ms), callback(on_trigger) {
        tick();
    }

    void tick() {
        last_tick.store(now());
    }

    void poll() {
        uint64_t n = now();
        uint64_t lt = last_tick.load();
        if (n - lt > max_idle * 1000000ULL) {
            callback();
        }
    }

private:
    std::atomic<uint64_t> last_tick{0};
    uint64_t max_idle;
    std::function<void()> callback;

    static uint64_t now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()
        ).count();
    }
};

}
