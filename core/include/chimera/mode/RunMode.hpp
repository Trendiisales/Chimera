#pragma once

#include <atomic>
#include <stdexcept>

namespace chimera::mode {

enum class RunMode : uint8_t {
    LIVE = 0,
    REPLAY = 1,
    SHADOW = 2  // For parallel shadow testing
};

// Global mode - thread-safe
class ModeGuard {
private:
    static inline std::atomic<RunMode> current_mode_{RunMode::LIVE};
    static inline std::atomic<bool> locked_{false};

public:
    // Get current mode
    static RunMode get() noexcept {
        return current_mode_.load(std::memory_order_acquire);
    }

    // Set mode (only if not locked)
    static bool set(RunMode mode) noexcept {
        if (locked_.load(std::memory_order_acquire)) {
            return false;
        }
        current_mode_.store(mode, std::memory_order_release);
        return true;
    }

    // Lock mode (cannot be changed after this)
    static void lock() noexcept {
        locked_.store(true, std::memory_order_release);
    }

    // Check if in replay mode
    static bool is_replay() noexcept {
        return get() == RunMode::REPLAY;
    }

    // Check if in live mode
    static bool is_live() noexcept {
        return get() == RunMode::LIVE;
    }

    // Check if in shadow mode
    static bool is_shadow() noexcept {
        return get() == RunMode::SHADOW;
    }

    // Reset (for testing only)
    static void reset_for_testing() noexcept {
        locked_.store(false, std::memory_order_release);
        current_mode_.store(RunMode::LIVE, std::memory_order_release);
    }
};

// RAII mode setter
class ScopedMode {
private:
    RunMode prev_mode_;
    bool was_locked_;

public:
    explicit ScopedMode(RunMode mode) 
        : prev_mode_(ModeGuard::get())
        , was_locked_(false)
    {
        if (!ModeGuard::set(mode)) {
            throw std::runtime_error("Cannot change mode - already locked");
        }
    }

    ~ScopedMode() {
        if (!was_locked_) {
            ModeGuard::set(prev_mode_);
        }
    }

    // Lock at this scope
    void lock() {
        ModeGuard::lock();
        was_locked_ = true;
    }

    ScopedMode(const ScopedMode&) = delete;
    ScopedMode& operator=(const ScopedMode&) = delete;
};

} // namespace chimera::mode
