// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/bootstrap/BootstrapEvaluator.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Information-based bootstrap with PROBE_LATENCY phase for HFT
// OWNER: Jo
// CREATED: 2024-12-27
//
// v4.9.27: CRITICAL FIX - Added PROBE_LATENCY state
//   Bootstrap MUST measure real exchange latency before live trading.
//   Previous version had circular dependency bug:
//     - Needed intents to advance
//     - But intents only come from orders
//     - But orders blocked until bootstrap complete
//   
//   NEW 4-STATE MACHINE:
//   1. WAIT_DATA       - Feed quality (book, EMAs, VPIN populated)
//   2. PROBE_LATENCY   - Send probe orders, measure ACK latency (NEW!)
//   3. WAIT_EDGE       - Signal quality validation (optional, fast)
//   4. COMPLETE        - Ready for live trading
//
// PRINCIPLE: "Can't trade HFT without knowing your latency"
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <atomic>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Chimera {
namespace Bootstrap {

// ─────────────────────────────────────────────────────────────────────────────
// Bootstrap State Machine - v4.9.27 with PROBE_LATENCY
// ─────────────────────────────────────────────────────────────────────────────
enum class BootstrapState : uint8_t {
    INIT = 0,
    WAIT_DATA,          // Waiting for feed quality
    PROBE_LATENCY,      // v4.9.27: Sending probe orders to measure latency
    WAIT_EDGE,          // Brief signal validation (fast pass-through)
    COMPLETE            // Ready for live trading
};

inline const char* state_str(BootstrapState s) {
    switch (s) {
        case BootstrapState::INIT:          return "INIT";
        case BootstrapState::WAIT_DATA:     return "WAIT_DATA";
        case BootstrapState::PROBE_LATENCY: return "PROBE_LATENCY";
        case BootstrapState::WAIT_EDGE:     return "WAIT_EDGE";
        case BootstrapState::COMPLETE:      return "COMPLETE";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Intent Record (shadow signal, not execution)
// ─────────────────────────────────────────────────────────────────────────────
struct IntentRecord {
    uint64_t timestamp_ns = 0;
    int8_t direction = 0;     // +1 buy, -1 sell, 0 none
    double edge_bps = 0.0;
    double spread_bps = 0.0;
    uint8_t regime = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Bootstrap Evaluator - Per Symbol
// ─────────────────────────────────────────────────────────────────────────────
class BootstrapEvaluator {
public:
    // ═══════════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════════
    struct Config {
        // DATA_READY thresholds
        uint64_t min_book_valid_ms;
        uint32_t min_tick_count;
        uint32_t min_spread_samples;
        
        // PROBE_LATENCY thresholds (v4.9.27)
        uint32_t min_probe_acks;        // Minimum ACKs needed
        uint32_t max_probe_attempts;    // Max probes before giving up
        uint64_t probe_timeout_ms;      // Timeout per probe
        
        // WAIT_EDGE thresholds (relaxed - probes already validated execution path)
        uint32_t min_intents;
        double max_churn_rate;
        double min_persistence;
        double min_mean_edge_bps;
        
        // SAFETY_READY
        bool require_kill_switch;
        bool require_spread_guard;
        
        Config()
            : min_book_valid_ms(30'000)     // Book stable for 30s
            , min_tick_count(100)           // Minimum ticks processed
            , min_spread_samples(200)       // Spread baseline samples
            // v4.9.27: PROBE_LATENCY config
            , min_probe_acks(3)             // Need 3 ACKs minimum
            , max_probe_attempts(10)        // Try up to 10 probes
            , probe_timeout_ms(5000)        // 5s timeout per probe
            // WAIT_EDGE (relaxed since probes validate path)
            , min_intents(3)                // Just 3 intents after probes
            , max_churn_rate(0.50)          // Allow 50% churn (relaxed)
            , min_persistence(0.50)         // 50% persistence (relaxed)
            , min_mean_edge_bps(0.1)        // 0.1bps min edge (relaxed)
            , require_kill_switch(true)
            , require_spread_guard(true)
        {}
    };
    
    explicit BootstrapEvaluator(const std::string& symbol, const Config& cfg = Config{})
        : symbol_(symbol)
        , config_(cfg)
        , state_(BootstrapState::INIT)
    {
        reset();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Data Feed Observation (call on every tick)
    // ═══════════════════════════════════════════════════════════════════════
    void observe_tick(
        double spread_bps,
        double bid,
        double ask,
        uint64_t now_ns
    ) noexcept {
        tick_count_++;
        
        // Track book validity duration
        bool book_valid = (spread_bps > 0.0 && bid > 0.0 && ask > 0.0 && bid < ask);
        if (book_valid) {
            if (book_valid_start_ns_ == 0) {
                book_valid_start_ns_ = now_ns;
            }
            book_valid_duration_ms_ = (now_ns - book_valid_start_ns_) / 1'000'000;
        } else {
            book_valid_start_ns_ = 0;
            book_valid_duration_ms_ = 0;
        }
        
        // Track spread samples
        if (spread_bps > 0.0) {
            spread_sample_count_++;
            spread_sum_ += spread_bps;
        }
        
        last_tick_ns_ = now_ns;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.27: Probe Order Tracking - THE KEY TO LATENCY MEASUREMENT
    // ═══════════════════════════════════════════════════════════════════════
    
    // Call when sending a probe order
    void probe_sent(uint64_t order_id, uint64_t sent_ns) noexcept {
        if (state_ != BootstrapState::PROBE_LATENCY) return;
        
        probe_attempts_++;
        last_probe_id_ = order_id;
        last_probe_sent_ns_ = sent_ns;
        
        std::cout << "[BOOTSTRAP-" << symbol_ << "] PROBE #" << probe_attempts_ 
                  << " sent (id=" << order_id << ")\n";
    }
    
    // Call when ACK received for probe order
    void probe_ack(uint64_t order_id, uint64_t ack_ns) noexcept {
        if (state_ != BootstrapState::PROBE_LATENCY) return;
        if (order_id != last_probe_id_) return;  // Wrong order
        
        probe_acks_++;
        uint64_t latency_ns = ack_ns - last_probe_sent_ns_;
        double latency_ms = latency_ns / 1'000'000.0;
        
        // Track latency stats
        latency_sum_ms_ += latency_ms;
        if (latency_ms < latency_min_ms_ || latency_min_ms_ == 0) latency_min_ms_ = latency_ms;
        if (latency_ms > latency_max_ms_) latency_max_ms_ = latency_ms;
        
        std::cout << "[BOOTSTRAP-" << symbol_ << "] PROBE ACK #" << probe_acks_ 
                  << " latency=" << std::fixed << std::setprecision(2) << latency_ms << "ms"
                  << " (min=" << latency_min_ms_ << " avg=" << (latency_sum_ms_/probe_acks_) 
                  << " max=" << latency_max_ms_ << ")\n";
    }
    
    // Call when probe rejected (signature error, etc)
    void probe_rejected(uint64_t order_id, int error_code, const char* reason) noexcept {
        (void)order_id;  // Suppress unused warning - may use for tracking later
        if (state_ != BootstrapState::PROBE_LATENCY) return;
        
        probe_rejects_++;
        std::cout << "[BOOTSTRAP-" << symbol_ << "] ⚠ PROBE REJECTED #" << probe_rejects_
                  << " error=" << error_code << " reason=" << (reason ? reason : "unknown") << "\n";
    }
    
    // Check if should send probe now
    [[nodiscard]] bool should_send_probe() const noexcept {
        if (state_ != BootstrapState::PROBE_LATENCY) return false;
        if (probe_acks_ >= config_.min_probe_acks) return false;  // Already have enough
        if (probe_attempts_ >= config_.max_probe_attempts) return false;  // Hit limit
        
        // Rate limit: wait for previous probe to complete or timeout
        if (last_probe_sent_ns_ > 0 && probe_acks_ < probe_attempts_) {
            auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            uint64_t elapsed_ms = (now - last_probe_sent_ns_) / 1'000'000;
            if (elapsed_ms < config_.probe_timeout_ms) return false;  // Still waiting
        }
        
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Intent Observation (call when signal generated)
    // ═══════════════════════════════════════════════════════════════════════
    void observe_intent(
        int8_t direction,
        double edge_bps,
        double spread_bps,
        uint8_t regime,
        uint64_t now_ns
    ) noexcept {
        if (direction == 0) return;
        
        auto& intent = intents_[intent_idx_];
        intent.timestamp_ns = now_ns;
        intent.direction = direction;
        intent.edge_bps = edge_bps;
        intent.spread_bps = spread_bps;
        intent.regime = regime;
        
        intent_idx_ = (intent_idx_ + 1) % MAX_INTENTS;
        intent_count_++;
        
        if (last_direction_ != 0 && direction != last_direction_) {
            direction_flips_++;
        }
        last_direction_ = direction;
        
        if (direction > 0) buy_intents_++;
        else sell_intents_++;
        
        edge_sum_ += edge_bps;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Safety Observation
    // ═══════════════════════════════════════════════════════════════════════
    void observe_safety(
        bool kill_switch_armed,
        bool spread_guard_active,
        bool edge_guard_active
    ) noexcept {
        kill_switch_armed_ = kill_switch_armed;
        spread_guard_active_ = spread_guard_active;
        edge_guard_active_ = edge_guard_active;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Evaluate Bootstrap State (call periodically)
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] bool evaluate() noexcept {
        if (state_ == BootstrapState::COMPLETE) {
            return true;
        }
        
        // State transitions
        if (state_ == BootstrapState::INIT) {
            state_ = BootstrapState::WAIT_DATA;
        }
        
        // WAIT_DATA → PROBE_LATENCY
        if (state_ == BootstrapState::WAIT_DATA && check_data_ready()) {
            state_ = BootstrapState::PROBE_LATENCY;
            std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ DATA_READY - advancing to PROBE_LATENCY\n";
            std::cout << "[BOOTSTRAP-" << symbol_ << "] Will send up to " << config_.max_probe_attempts 
                      << " probes, need " << config_.min_probe_acks << " ACKs\n";
        }
        
        // PROBE_LATENCY → WAIT_EDGE (or COMPLETE if probes successful)
        if (state_ == BootstrapState::PROBE_LATENCY) {
            if (probe_acks_ >= config_.min_probe_acks) {
                // Success! Got enough ACKs
                state_ = BootstrapState::WAIT_EDGE;
                std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ PROBE_LATENCY complete - "
                          << probe_acks_ << " ACKs, avg latency=" 
                          << std::fixed << std::setprecision(2) << (latency_sum_ms_/probe_acks_) << "ms\n";
            } else if (probe_attempts_ >= config_.max_probe_attempts) {
                // Failed - too many attempts without enough ACKs
                std::cout << "[BOOTSTRAP-" << symbol_ << "] ⚠ PROBE_LATENCY FAILED - "
                          << probe_acks_ << "/" << config_.min_probe_acks << " ACKs after "
                          << probe_attempts_ << " attempts, " << probe_rejects_ << " rejects\n";
                // Stay in PROBE_LATENCY - don't advance with broken execution path
            }
        }
        
        // WAIT_EDGE → COMPLETE (fast pass-through since probes validated path)
        if (state_ == BootstrapState::WAIT_EDGE) {
            bool edge_ok = check_edge_ready();
            bool safety_ok = check_safety_ready();
            
            if (edge_ok && safety_ok) {
                state_ = BootstrapState::COMPLETE;
                std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ COMPLETE - trading enabled\n";
                print_summary();
            } else {
                // Fast-track: if we have probe data, skip edge requirements
                // Probe success means execution path works
                if (probe_acks_ >= config_.min_probe_acks && safety_ok) {
                    state_ = BootstrapState::COMPLETE;
                    std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ COMPLETE (fast-track via probes)\n";
                    print_summary();
                }
            }
        }
        
        // Periodic status logging
        static thread_local uint64_t log_counter = 0;
        if (++log_counter % 2000 == 1 && state_ != BootstrapState::COMPLETE) {
            print_status();
        }
        
        return state_ == BootstrapState::COMPLETE;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Accessors
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] bool is_complete() const noexcept { 
        return state_ == BootstrapState::COMPLETE; 
    }
    
    [[nodiscard]] bool in_probe_phase() const noexcept {
        return state_ == BootstrapState::PROBE_LATENCY;
    }
    
    [[nodiscard]] BootstrapState state() const noexcept { return state_; }
    [[nodiscard]] const char* state_string() const noexcept { return state_str(state_); }
    
    [[nodiscard]] uint32_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] uint32_t intent_count() const noexcept { return intent_count_; }
    [[nodiscard]] uint64_t book_valid_ms() const noexcept { return book_valid_duration_ms_; }
    
    // v4.9.27: Probe stats
    [[nodiscard]] uint32_t probe_attempts() const noexcept { return probe_attempts_; }
    [[nodiscard]] uint32_t probe_acks() const noexcept { return probe_acks_; }
    [[nodiscard]] uint32_t probe_rejects() const noexcept { return probe_rejects_; }
    [[nodiscard]] double latency_avg_ms() const noexcept { 
        return probe_acks_ > 0 ? latency_sum_ms_ / probe_acks_ : 0.0; 
    }
    [[nodiscard]] double latency_min_ms() const noexcept { return latency_min_ms_; }
    [[nodiscard]] double latency_max_ms() const noexcept { return latency_max_ms_; }
    
    [[nodiscard]] double churn_rate() const noexcept {
        if (intent_count_ < 2) return 0.0;
        return static_cast<double>(direction_flips_) / (intent_count_ - 1);
    }
    
    [[nodiscard]] double persistence() const noexcept {
        uint32_t total = buy_intents_ + sell_intents_;
        if (total == 0) return 0.5;
        return static_cast<double>(std::max(buy_intents_, sell_intents_)) / total;
    }
    
    [[nodiscard]] double mean_edge_bps() const noexcept {
        if (intent_count_ == 0) return 0.0;
        return edge_sum_ / intent_count_;
    }
    
    [[nodiscard]] bool data_ready() const noexcept { return check_data_ready(); }
    [[nodiscard]] bool edge_ready() const noexcept { return check_edge_ready(); }
    [[nodiscard]] bool safety_ready() const noexcept { return check_safety_ready(); }
    
    void reset() noexcept {
        state_ = BootstrapState::INIT;
        tick_count_ = 0;
        book_valid_start_ns_ = 0;
        book_valid_duration_ms_ = 0;
        spread_sample_count_ = 0;
        spread_sum_ = 0.0;
        
        // v4.9.27: Reset probe stats
        probe_attempts_ = 0;
        probe_acks_ = 0;
        probe_rejects_ = 0;
        last_probe_id_ = 0;
        last_probe_sent_ns_ = 0;
        latency_sum_ms_ = 0.0;
        latency_min_ms_ = 0.0;
        latency_max_ms_ = 0.0;
        
        intent_count_ = 0;
        intent_idx_ = 0;
        direction_flips_ = 0;
        last_direction_ = 0;
        buy_intents_ = 0;
        sell_intents_ = 0;
        edge_sum_ = 0.0;
        
        kill_switch_armed_ = false;
        spread_guard_active_ = false;
        edge_guard_active_ = false;
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // Gate Checks
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] bool check_data_ready() const noexcept {
        return book_valid_duration_ms_ >= config_.min_book_valid_ms
            && tick_count_ >= config_.min_tick_count
            && spread_sample_count_ >= config_.min_spread_samples;
    }
    
    [[nodiscard]] bool check_edge_ready() const noexcept {
        if (intent_count_ < config_.min_intents) return false;
        
        double churn = churn_rate();
        double persist = persistence();
        double edge = mean_edge_bps();
        
        return churn <= config_.max_churn_rate
            && persist >= config_.min_persistence
            && edge >= config_.min_mean_edge_bps;
    }
    
    [[nodiscard]] bool check_safety_ready() const noexcept {
        if (config_.require_kill_switch && !kill_switch_armed_) return false;
        if (config_.require_spread_guard && !spread_guard_active_) return false;
        return true;
    }
    
    void print_status() const {
        std::cout << "[BOOTSTRAP-" << symbol_ << "] State=" << state_str(state_);
        
        if (state_ == BootstrapState::WAIT_DATA) {
            std::cout << " | DATA: book=" << book_valid_duration_ms_ << "ms/" << config_.min_book_valid_ms
                      << " ticks=" << tick_count_ << "/" << config_.min_tick_count
                      << " spread=" << spread_sample_count_ << "/" << config_.min_spread_samples;
        } else if (state_ == BootstrapState::PROBE_LATENCY) {
            std::cout << " | PROBE: attempts=" << probe_attempts_ << "/" << config_.max_probe_attempts
                      << " acks=" << probe_acks_ << "/" << config_.min_probe_acks
                      << " rejects=" << probe_rejects_;
            if (probe_acks_ > 0) {
                std::cout << " | LAT: avg=" << std::fixed << std::setprecision(2) 
                          << (latency_sum_ms_/probe_acks_) << "ms"
                          << " min=" << latency_min_ms_ << " max=" << latency_max_ms_;
            }
        } else if (state_ == BootstrapState::WAIT_EDGE) {
            std::cout << " | EDGE: intents=" << intent_count_ << "/" << config_.min_intents
                      << " churn=" << std::fixed << std::setprecision(1) << (churn_rate()*100) << "%"
                      << " persist=" << (persistence()*100) << "%"
                      << " | SAFETY: kill=" << (kill_switch_armed_ ? "Y" : "N")
                      << " spread=" << (spread_guard_active_ ? "Y" : "N");
        }
        
        std::cout << "\n";
    }
    
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  BOOTSTRAP COMPLETE: " << symbol_ << std::setw(30) << " ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  ✓ DATA_READY                                                ║\n";
        std::cout << "║    Book valid: " << std::setw(6) << book_valid_duration_ms_ << "ms                                 ║\n";
        std::cout << "║    Ticks: " << std::setw(6) << tick_count_ << "                                       ║\n";
        std::cout << "║  ✓ PROBE_LATENCY                                             ║\n";
        std::cout << "║    Probes: " << std::setw(3) << probe_acks_ << "/" << probe_attempts_ << " ACKs                                   ║\n";
        std::cout << "║    Latency: " << std::fixed << std::setprecision(2) << std::setw(6) 
                  << (probe_acks_ > 0 ? latency_sum_ms_/probe_acks_ : 0.0) << "ms avg                              ║\n";
        std::cout << "║  ✓ SAFETY_READY                                              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Data Members
    // ═══════════════════════════════════════════════════════════════════════
    std::string symbol_;
    Config config_;
    BootstrapState state_;
    
    // Data readiness
    uint32_t tick_count_ = 0;
    uint64_t book_valid_start_ns_ = 0;
    uint64_t book_valid_duration_ms_ = 0;
    uint32_t spread_sample_count_ = 0;
    double spread_sum_ = 0.0;
    uint64_t last_tick_ns_ = 0;
    
    // v4.9.27: Probe latency tracking
    uint32_t probe_attempts_ = 0;
    uint32_t probe_acks_ = 0;
    uint32_t probe_rejects_ = 0;
    uint64_t last_probe_id_ = 0;
    uint64_t last_probe_sent_ns_ = 0;
    double latency_sum_ms_ = 0.0;
    double latency_min_ms_ = 0.0;
    double latency_max_ms_ = 0.0;
    
    // Intent tracking
    static constexpr size_t MAX_INTENTS = 256;
    std::array<IntentRecord, MAX_INTENTS> intents_;
    uint32_t intent_idx_ = 0;
    uint32_t intent_count_ = 0;
    
    // Edge quality
    uint32_t direction_flips_ = 0;
    int8_t last_direction_ = 0;
    uint32_t buy_intents_ = 0;
    uint32_t sell_intents_ = 0;
    double edge_sum_ = 0.0;
    
    // Safety
    bool kill_switch_armed_ = false;
    bool spread_guard_active_ = false;
    bool edge_guard_active_ = false;
};

} // namespace Bootstrap
} // namespace Chimera
