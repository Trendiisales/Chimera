#pragma once

#include <atomic>

namespace chimera {

// ---------------------------------------------------------------------------
// Cancel Federation — centralized kill-sweep signal.
//
// Any safety governor calls trigger() from any thread. This is just an atomic
// CAS + pointer store — zero blocking, zero REST, zero locks.
//
// The actual sweep (cancel all in-flight via REST, clear coalescer, drift kill)
// runs on CORE1 inside ExecutionRouter::poll() where live_client_ lives.
// CURL handles are not thread-safe — the sweep MUST run there.
//
// Max latency: trigger() → sweep = one poll tick (50µs on CORE1).
//
// Shadow mode: trigger() is a no-op (nothing to cancel on exchange, and
// drift kill will fire from the governor that triggered us anyway).
// ---------------------------------------------------------------------------
class CancelFederation {
public:
    // Signal cancel sweep. Only first caller wins (CAS). reason must be a
    // string literal (raw pointer stored, not copied — lifetime is infinite).
    void trigger(const char* reason) {
        bool expected = false;
        if (active_.compare_exchange_strong(expected, true,
                std::memory_order_release, std::memory_order_relaxed)) {
            reason_ = reason;
        }
    }

    bool active() const {
        return active_.load(std::memory_order_acquire);
    }

    const char* reason() const { return reason_; }

    void clear() {
        reason_ = nullptr;
        active_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> active_{false};
    const char*       reason_{nullptr};
};

} // namespace chimera
