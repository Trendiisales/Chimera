#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#include "runtime/Context.hpp"
#include "tier1/SignalRing.hpp"
#include "tier1/AtomicPositionGate.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace chimera {

// Forward declarations
class BinanceWSExecution;
class BinanceRestClient;

// ---------------------------------------------------------------------------
// Tier1ExecutionRouter: Drop-in replacement for ExecutionRouter
// 
// Key differences:
//   - Lock-free signal submission via SignalRing
//   - Atomic position gate (no mutexes)
//   - Single-writer router thread on pinned core
//   - Same external API as old ExecutionRouter
// 
// Migration:
//   1. Replace ExecutionRouter with Tier1ExecutionRouter in main()
//   2. Call start() after construction
//   3. Update strategies to check caps before submitting
// ---------------------------------------------------------------------------

class Tier1ExecutionRouter {
public:
    explicit Tier1ExecutionRouter(Context& ctx, AtomicPositionGate& gate)
        : ctx_(ctx), gate_(gate) {}

    ~Tier1ExecutionRouter() {
        stop();
    }

    // Start router thread (call after construction, before strategies start)
    void start() {
        if (running_.load())
            return;
        
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { run(); });
    }

    // Stop router thread (call during shutdown)
    void stop() {
        if (!running_.load())
            return;
            
        running_.store(false, std::memory_order_release);
        if (worker_.joinable())
            worker_.join();
    }

    // Wire execution clients (same as old router)
    void set_ws_exec(BinanceWSExecution* client) { ws_exec_ = client; }
    void set_rest_client(BinanceRestClient* client) { rest_client_ = client; }

    // Main submission API (same signature as old router)
    bool submit_order(const std::string& client_id,
                      const std::string& symbol,
                      double price, double qty,
                      const std::string& engine_id) {
        
        // Build signal
        TradeSignal sig{};
        std::strncpy(sig.symbol, symbol.c_str(), 11);
        sig.symbol[11] = '\0';
        std::strncpy(sig.engine_id, engine_id.c_str(), 11);
        sig.engine_id[11] = '\0';
        sig.qty = qty;
        sig.price = price;
        sig.edge_bps = 0.0;  // TODO: Pass edge from strategy
        sig.ts_submit = now_ns();
        sig.reduce_only = false;
        
        // Push to ring (lock-free)
        return ring_.push(sig);
    }

    // Reduce-only submission
    bool submit_reduce_only(const std::string& client_id,
                            const std::string& symbol,
                            double price, double qty,
                            const std::string& engine_id) {
        
        TradeSignal sig{};
        std::strncpy(sig.symbol, symbol.c_str(), 11);
        sig.symbol[11] = '\0';
        std::strncpy(sig.engine_id, engine_id.c_str(), 11);
        sig.engine_id[11] = '\0';
        sig.qty = qty;
        sig.price = price;
        sig.edge_bps = 0.0;
        sig.ts_submit = now_ns();
        sig.reduce_only = true;
        
        return ring_.push(sig);
    }

    // Polling (for compatibility - router runs in dedicated thread now)
    void poll() {
        // No-op in Tier1 - router thread handles everything
        // Keep this method for API compatibility
    }

    // Expose gate for cap management
    AtomicPositionGate& gate() { return gate_; }

    // Get position (thread-safe read)
    double get_position(const std::string& symbol) const {
        return gate_.get_position(symbol);
    }

    // Set cap (typically called during init)
    void set_cap(const std::string& symbol, double cap) {
        gate_.set_cap(symbol, cap);
    }

private:
    // Router thread main loop
    void run() {
        pin_core();
        
        std::cout << "[TIER1_ROUTER] Thread started on core 0" << std::endl;
        
        TradeSignal sig;
        while (running_.load(std::memory_order_relaxed)) {
            // Drain ring buffer
            while (ring_.pop(sig)) {
                process(sig);
            }
            
            // Brief yield to prevent busy-spin
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        std::cout << "[TIER1_ROUTER] Thread stopped" << std::endl;
    }

    // Process a single signal
    void process(const TradeSignal& sig) {
        std::string sym(sig.symbol);
        std::string engine(sig.engine_id);
        
        // Check if reduce-only
        if (!sig.reduce_only) {
            // Normal order - check position cap
            if (!gate_.allow(sym, sig.qty)) {
                // Would violate cap - reject (but don't spam log)
                rate_limited_log("[TIER1_ROUTER] BLOCK " + engine + " " + sym + " would exceed cap");
                return;
            }
        } else {
            // Reduce-only - verify it actually reduces
            double pos = gate_.get_position(sym);
            double next = pos + sig.qty;
            if (std::abs(next) >= std::abs(pos)) {
                rate_limited_log("[TIER1_ROUTER] BLOCK " + engine + " " + sym + " reduce-only but doesn't reduce");
                return;
            }
        }
        
        // === CRITICAL SECTION: Single-writer position update ===
        // Apply fill to position (shadow mode = instant)
        gate_.apply_fill(sym, sig.qty);
        
        // TODO: Wire to actual exchange when live
        // if (ws_exec_ && ctx_.arm.is_armed()) {
        //     ws_exec_->submit_order(...);
        // }
        
        // TODO: Update telemetry, journal, etc.
        // ctx_.telemetry.record_fill(engine, sym, sig.qty, sig.price);
        
        // Log fill (rate-limited)
        rate_limited_log("[TIER1_ROUTER] FILL " + engine + " " + sym + 
                        " qty=" + std::to_string(sig.qty) +
                        " pos=" + std::to_string(gate_.get_position(sym)));
    }

    // Pin router thread to core 0
    void pin_core() {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        
        int rc = pthread_setaffinity_np(
            pthread_self(),
            sizeof(cpu_set_t),
            &cpuset
        );
        
        if (rc != 0) {
            std::cerr << "[TIER1_ROUTER] Warning: Failed to pin to core 0" << std::endl;
        } else {
            std::cout << "[TIER1_ROUTER] Pinned to core 0" << std::endl;
        }
#endif
    }

    // Rate-limited logging to prevent spam
    void rate_limited_log(const std::string& msg) {
        uint64_t now = now_ns();
        if (now - last_log_ns_ > log_interval_ns_) {
            std::cout << msg << std::endl;
            last_log_ns_ = now;
        }
    }

    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    Context& ctx_;
    AtomicPositionGate& gate_;
    SignalRing<4096> ring_;
    
    BinanceWSExecution* ws_exec_{nullptr};
    BinanceRestClient* rest_client_{nullptr};
    
    std::atomic<bool> running_{false};
    std::thread worker_;
    
    // Rate limiting
    uint64_t last_log_ns_{0};
    uint64_t log_interval_ns_{100'000'000};  // 100ms
};

} // namespace chimera
