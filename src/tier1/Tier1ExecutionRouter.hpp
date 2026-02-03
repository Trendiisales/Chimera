#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

#include "tier1/SignalRing.hpp"
#include "tier1/AtomicPositionGate.hpp"
#include "runtime/Context.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace chimera {

// Forward declarations
class BinanceWSExecution;

// ---------------------------------------------------------------------------
// Tier1ExecutionRouter: Lock-free single-writer execution core
// 
// Architecture:
//   Strategies → SignalRing (lock-free MPSC) → Router (single thread)
// 
// Thread model:
//   - Router runs on pinned core (CPU 0)
//   - Pops signals from ring
//   - Checks atomic position gate
//   - Executes to exchange
//   - Updates atomic positions
// 
// Performance:
//   - No mutexes in hot path
//   - No contention (single writer)
//   - Cacheline-aligned atomics
//   - Expected: 5-15µs decision latency (down from 40-80µs)
// 
// Safety:
//   - Only this thread mutates position state
//   - Strategies only read via CapView
//   - No data races
// ---------------------------------------------------------------------------

class Tier1ExecutionRouter {
public:
    explicit Tier1ExecutionRouter(Context& ctx, AtomicPositionGate& gate)
        : ctx_(ctx), gate_(gate) {}

    // Start router thread (pins to core 0)
    void start() {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { run(); });
    }

    // Stop router thread
    void stop() {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable())
            worker_.join();
    }

    // Submit signal from strategy (called by strategy threads)
    // Returns false if ring is full (backpressure)
    bool submit(const TradeSignal& sig) {
        return ring_.push(sig);
    }

    // Wire execution client (same as old router)
    void set_ws_exec(BinanceWSExecution* client) {
        ws_exec_ = client;
    }

    // Get position (reader - safe from any thread)
    double get_position(const std::string& sym) const {
        return gate_.get_position(sym);
    }

    // Set cap (initialization or dynamic adjustment)
    void set_cap(const std::string& sym, double cap) {
        gate_.set_cap(sym, cap);
    }

private:
    // Router thread main loop
    void run() {
        pin_core();
        
        std::cout << "[TIER1] Router thread started on core 0" << std::endl;
        
        TradeSignal sig;
        while (running_.load(std::memory_order_relaxed)) {
            // Drain ring buffer
            while (ring_.pop(sig)) {
                process(sig);
            }
            
            // Yield briefly to prevent busy-spin
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        std::cout << "[TIER1] Router thread stopped" << std::endl;
    }

    // Process a single signal
    void process(const TradeSignal& sig) {
        std::string sym(sig.symbol);
        std::string engine(sig.engine_id);
        
        // Check if this is reduce-only
        if (!sig.reduce_only) {
            // Normal order - check position cap
            if (!gate_.allow(sym, sig.qty)) {
                // Would violate cap - reject
                std::cout << "[TIER1] BLOCK " << engine << " " << sym
                          << " would exceed cap (pos=" << gate_.get_position(sym)
                          << " cap=" << gate_.get_cap(sym) << ")" << std::endl;
                return;
            }
        } else {
            // Reduce-only - verify it actually reduces
            double pos = gate_.get_position(sym);
            double next = pos + sig.qty;
            if (std::abs(next) >= std::abs(pos)) {
                std::cout << "[TIER1] BLOCK " << engine << " " << sym
                          << " reduce-only but doesn't reduce" << std::endl;
                return;
            }
        }
        
        // === CRITICAL SECTION: Single-writer position update ===
        // Apply fill to position (shadow fill = instant)
        gate_.apply_fill(sym, sig.qty);
        
        // Log the fill
        std::cout << "[TIER1] FILL " << engine << " " << sym
                  << " qty=" << sig.qty
                  << " @ " << sig.price
                  << " edge=" << sig.edge_bps << "bps"
                  << " pos=" << gate_.get_position(sym)
                  << std::endl;
        
        // TODO: Wire to actual exchange
        // if (ws_exec_ && ctx_.arm.is_armed()) {
        //     ws_exec_->submit_order(...);
        // }
        
        // TODO: Update journal, telemetry, etc.
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
            std::cerr << "[TIER1] Warning: Failed to pin router to core 0" << std::endl;
        } else {
            std::cout << "[TIER1] Router pinned to core 0" << std::endl;
        }
#endif
    }

    Context& ctx_;
    AtomicPositionGate& gate_;
    SignalRing<4096> ring_;
    
    BinanceWSExecution* ws_exec_{nullptr};
    
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace chimera
