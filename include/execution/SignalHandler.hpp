// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/SignalHandler.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: HARDENED SHUTDOWN + RECOVERY
//
// PURPOSE: Crash-proof trading operations.
// Ensures no orphan orders, no zombie positions on shutdown.
//
// HANDLES:
// - SIGINT (Ctrl+C)
// - SIGTERM (kill)
// - SIGQUIT (core dump request)
// - SIGSEGV (crash recovery attempt)
//
// SHUTDOWN SEQUENCE:
// 1. Cancel all open orders
// 2. Flatten all positions (optional)
// 3. Persist profiles and state
// 4. Stop all engines
// 5. Exit cleanly
//
// INTEGRATION:
// Call installSignalHandlers() at startup
// Register shutdown callbacks via registerShutdownCallback()
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <csignal>
#include <cstdlib>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <iostream>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown Callback Type
// ─────────────────────────────────────────────────────────────────────────────
using ShutdownCallback = std::function<void()>;

// ─────────────────────────────────────────────────────────────────────────────
// Global Shutdown State (signal-safe)
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {
    inline std::atomic<bool> g_shutdown_requested{false};
    inline std::atomic<bool> g_shutdown_in_progress{false};
    inline std::atomic<int> g_shutdown_signal{0};
    
    // Callbacks must be registered before signals can occur
    // Not signal-safe to modify during signal handling
    inline std::vector<ShutdownCallback> g_shutdown_callbacks;
    inline std::mutex g_callback_mutex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Check if Shutdown Requested (safe to call from anywhere)
// ─────────────────────────────────────────────────────────────────────────────
inline bool isShutdownRequested() {
    return detail::g_shutdown_requested.load(std::memory_order_acquire);
}

inline int shutdownSignal() {
    return detail::g_shutdown_signal.load(std::memory_order_acquire);
}

// ─────────────────────────────────────────────────────────────────────────────
// Register Shutdown Callback (call at startup, not during signal)
// ─────────────────────────────────────────────────────────────────────────────
inline void registerShutdownCallback(ShutdownCallback cb) {
    std::lock_guard<std::mutex> lock(detail::g_callback_mutex);
    detail::g_shutdown_callbacks.push_back(std::move(cb));
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute Shutdown Callbacks (called from signal handler context-safe wrapper)
// ─────────────────────────────────────────────────────────────────────────────
inline void executeShutdownCallbacks() {
    // Only run once
    bool expected = false;
    if (!detail::g_shutdown_in_progress.compare_exchange_strong(expected, true)) {
        return;  // Already in progress
    }
    
    std::cerr << "\n[SHUTDOWN] Executing " << detail::g_shutdown_callbacks.size() 
              << " shutdown callbacks...\n";
    
    for (auto& cb : detail::g_shutdown_callbacks) {
        try {
            cb();
        } catch (...) {
            std::cerr << "[SHUTDOWN] Callback threw exception (ignored)\n";
        }
    }
    
    std::cerr << "[SHUTDOWN] Callbacks complete\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal Handler (async-signal-safe)
// ─────────────────────────────────────────────────────────────────────────────
inline void signalHandler(int sig) {
    // Mark shutdown requested (signal-safe)
    detail::g_shutdown_signal.store(sig, std::memory_order_release);
    detail::g_shutdown_requested.store(true, std::memory_order_release);
    
    // For SIGSEGV, try to do minimal cleanup
    if (sig == SIGSEGV) {
        // Can't do much safely here - just mark and exit
        std::_Exit(128 + sig);
    }
    
    // For normal signals, callbacks will be executed in main loop
    // Don't call std::exit() from signal handler - not safe
}

// ─────────────────────────────────────────────────────────────────────────────
// Install Signal Handlers (call once at startup)
// ─────────────────────────────────────────────────────────────────────────────
inline void installSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // Install for graceful shutdown signals
    sigaction(SIGINT, &sa, nullptr);   // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);  // kill
    sigaction(SIGQUIT, &sa, nullptr);  // quit
    
    // SIGSEGV - try to mark shutdown before crash
    sigaction(SIGSEGV, &sa, nullptr);
    
    std::cerr << "[SIGNALS] Signal handlers installed\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Graceful Shutdown (call from main loop when isShutdownRequested())
// ─────────────────────────────────────────────────────────────────────────────
inline void gracefulShutdown() {
    std::cerr << "\n[SHUTDOWN] Signal " << shutdownSignal() << " received, initiating shutdown...\n";
    
    // Execute callbacks
    executeShutdownCallbacks();
    
    // Exit cleanly
    std::_Exit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pre-built Shutdown Callbacks
// ─────────────────────────────────────────────────────────────────────────────

// Cancel all orders callback (template for integration)
template<typename OrderCancelFunc>
inline ShutdownCallback makeCancelOrdersCallback(OrderCancelFunc cancel_func) {
    return [cancel_func]() {
        std::cerr << "[SHUTDOWN] Cancelling all open orders...\n";
        cancel_func();
    };
}

// Flatten positions callback (template for integration)
template<typename FlattenFunc>
inline ShutdownCallback makeFlattenPositionsCallback(FlattenFunc flatten_func) {
    return [flatten_func]() {
        std::cerr << "[SHUTDOWN] Flattening all positions...\n";
        flatten_func();
    };
}

// Persist state callback (template for integration)
template<typename PersistFunc>
inline ShutdownCallback makePersistStateCallback(PersistFunc persist_func) {
    return [persist_func]() {
        std::cerr << "[SHUTDOWN] Persisting state...\n";
        persist_func();
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown Manager (helper class for coordinated shutdown)
// ─────────────────────────────────────────────────────────────────────────────
class ShutdownManager {
public:
    static ShutdownManager& instance() {
        static ShutdownManager inst;
        return inst;
    }
    
    void install() {
        if (installed_) return;
        installSignalHandlers();
        installed_ = true;
    }
    
    void addCallback(ShutdownCallback cb) {
        registerShutdownCallback(std::move(cb));
    }
    
    bool shouldShutdown() const {
        return isShutdownRequested();
    }
    
    void shutdown() {
        gracefulShutdown();
    }
    
    // Check in main loop - returns true if should exit
    bool checkAndHandleShutdown() {
        if (isShutdownRequested()) {
            gracefulShutdown();
            return true;
        }
        return false;
    }
    
private:
    ShutdownManager() = default;
    bool installed_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: Get global shutdown manager
// ─────────────────────────────────────────────────────────────────────────────
inline ShutdownManager& getShutdownManager() {
    return ShutdownManager::instance();
}

} // namespace Execution
} // namespace Chimera
