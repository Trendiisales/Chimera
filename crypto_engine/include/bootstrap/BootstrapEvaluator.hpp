// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/bootstrap/BootstrapEvaluator.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Information-based bootstrap - measures readiness, not execution count
// OWNER: Jo
// CREATED: 2024-12-27
//
// v4.2.4: REPLACES trade-count bootstrap with 3-gate system:
//   1. DATA_READY      - Feed quality (book, EMAs, VPIN populated)
//   2. EDGE_READY      - Signal quality (persistence, low churn)
//   3. SAFETY_READY    - Guards active
//
// PRINCIPLE: "Bootstrap measures information readiness, not execution"
// - Trades are OUTPUT, not INPUT to bootstrap
// - System can complete bootstrap with ZERO trades if market is ready
// - Protects against circular dependency
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <atomic>
#include <string>
#include <iostream>
#include <iomanip>

namespace Chimera {
namespace Bootstrap {

// ─────────────────────────────────────────────────────────────────────────────
// Bootstrap State Machine
// ─────────────────────────────────────────────────────────────────────────────
enum class BootstrapState : uint8_t {
    INIT = 0,
    WAIT_DATA,          // Waiting for feed quality
    WAIT_EDGE_QUALITY,  // Waiting for signal validation
    COMPLETE            // Ready for live trading
};

inline const char* state_str(BootstrapState s) {
    switch (s) {
        case BootstrapState::INIT:              return "INIT";
        case BootstrapState::WAIT_DATA:         return "WAIT_DATA";
        case BootstrapState::WAIT_EDGE_QUALITY: return "WAIT_EDGE";
        case BootstrapState::COMPLETE:          return "COMPLETE";
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
    // Configuration (conservative defaults)
    // ═══════════════════════════════════════════════════════════════════════
    struct Config {
        // DATA_READY thresholds
        uint64_t min_book_valid_ms;
        uint32_t min_tick_count;
        uint32_t min_spread_samples;
        
        // EDGE_READY thresholds
        uint32_t min_intents;
        double max_churn_rate;
        double min_persistence;
        double min_mean_edge_bps;
        
        // SAFETY_READY (mostly boolean checks)
        bool require_kill_switch;
        bool require_spread_guard;
        
        // Default constructor with conservative values
        Config()
            : min_book_valid_ms(30'000)     // Book stable for 30s
            , min_tick_count(100)           // Minimum ticks processed
            , min_spread_samples(200)       // Spread baseline samples
            , min_intents(40)               // Minimum signal intents
            , max_churn_rate(0.25)          // Max flip-flop rate (25%)
            , min_persistence(0.60)         // Min directional persistence (60%)
            , min_mean_edge_bps(0.5)        // Min average edge
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
    // Intent Observation (call when AllowTradeHFT would return true)
    // This is the KEY difference - we count SIGNALS, not EXECUTIONS
    // ═══════════════════════════════════════════════════════════════════════
    void observe_intent(
        int8_t direction,       // +1 buy, -1 sell
        double edge_bps,
        double spread_bps,
        uint8_t regime,
        uint64_t now_ns
    ) noexcept {
        if (direction == 0) return;
        
        // Store in ring buffer
        auto& intent = intents_[intent_idx_];
        intent.timestamp_ns = now_ns;
        intent.direction = direction;
        intent.edge_bps = edge_bps;
        intent.spread_bps = spread_bps;
        intent.regime = regime;
        
        intent_idx_ = (intent_idx_ + 1) % MAX_INTENTS;
        intent_count_++;
        
        // Track directional changes (churn)
        if (last_direction_ != 0 && direction != last_direction_) {
            direction_flips_++;
        }
        last_direction_ = direction;
        
        // Track directional persistence
        if (direction > 0) buy_intents_++;
        else sell_intents_++;
        
        // Track edge quality
        edge_sum_ += edge_bps;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Safety Observation (call periodically)
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
        
        // Evaluate gates in order
        bool data_ready = check_data_ready();
        bool edge_ready = check_edge_ready();
        bool safety_ready = check_safety_ready();
        
        // State transitions
        [[maybe_unused]] BootstrapState prev_state = state_;  // For debugging if needed
        
        if (state_ == BootstrapState::INIT) {
            state_ = BootstrapState::WAIT_DATA;
        }
        
        if (state_ == BootstrapState::WAIT_DATA && data_ready) {
            state_ = BootstrapState::WAIT_EDGE_QUALITY;
            std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ DATA_READY - advancing to WAIT_EDGE\n";
        }
        
        if (state_ == BootstrapState::WAIT_EDGE_QUALITY && edge_ready && safety_ready) {
            state_ = BootstrapState::COMPLETE;
            std::cout << "[BOOTSTRAP-" << symbol_ << "] ✓ COMPLETE - trading enabled\n";
            print_summary();
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
    
    [[nodiscard]] BootstrapState state() const noexcept { return state_; }
    [[nodiscard]] const char* state_string() const noexcept { return state_str(state_); }
    
    [[nodiscard]] uint32_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] uint32_t intent_count() const noexcept { return intent_count_; }
    [[nodiscard]] uint64_t book_valid_ms() const noexcept { return book_valid_duration_ms_; }
    
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
    
    // For GUI display
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
        std::cout << "[BOOTSTRAP-" << symbol_ << "] State=" << state_str(state_)
                  << " | DATA: book=" << book_valid_duration_ms_ << "ms/" << config_.min_book_valid_ms
                  << " ticks=" << tick_count_ << "/" << config_.min_tick_count
                  << " spread=" << spread_sample_count_ << "/" << config_.min_spread_samples
                  << " | EDGE: intents=" << intent_count_ << "/" << config_.min_intents
                  << " churn=" << std::fixed << std::setprecision(1) << (churn_rate()*100) << "%"
                  << " persist=" << (persistence()*100) << "%"
                  << " edge=" << std::setprecision(2) << mean_edge_bps() << "bps"
                  << " | SAFETY: kill=" << (kill_switch_armed_ ? "Y" : "N")
                  << " spread=" << (spread_guard_active_ ? "Y" : "N")
                  << "\n";
    }
    
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  BOOTSTRAP COMPLETE: " << symbol_ << std::setw(30) << " ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  ✓ DATA_READY                                                ║\n";
        std::cout << "║    Book valid: " << std::setw(6) << book_valid_duration_ms_ << "ms                                 ║\n";
        std::cout << "║    Ticks: " << std::setw(6) << tick_count_ << "                                       ║\n";
        std::cout << "║    Spread samples: " << std::setw(6) << spread_sample_count_ << "                              ║\n";
        std::cout << "║  ✓ EDGE_READY                                                ║\n";
        std::cout << "║    Intents: " << std::setw(6) << intent_count_ << "                                     ║\n";
        std::cout << "║    Churn: " << std::setw(5) << std::fixed << std::setprecision(1) << (churn_rate()*100) << "%                                       ║\n";
        std::cout << "║    Persistence: " << std::setw(5) << (persistence()*100) << "%                                  ║\n";
        std::cout << "║    Mean edge: " << std::setw(5) << std::setprecision(2) << mean_edge_bps() << "bps                                  ║\n";
        std::cout << "║  ✓ SAFETY_READY                                              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Data Members
    // ═══════════════════════════════════════════════════════════════════════
    std::string symbol_;
    Config config_;
    BootstrapState state_;
    
    // Data readiness tracking
    uint32_t tick_count_ = 0;
    uint64_t book_valid_start_ns_ = 0;
    uint64_t book_valid_duration_ms_ = 0;
    uint32_t spread_sample_count_ = 0;
    double spread_sum_ = 0.0;
    uint64_t last_tick_ns_ = 0;
    
    // Intent tracking (ring buffer)
    static constexpr size_t MAX_INTENTS = 256;
    std::array<IntentRecord, MAX_INTENTS> intents_;
    uint32_t intent_idx_ = 0;
    uint32_t intent_count_ = 0;
    
    // Edge quality metrics
    uint32_t direction_flips_ = 0;
    int8_t last_direction_ = 0;
    uint32_t buy_intents_ = 0;
    uint32_t sell_intents_ = 0;
    double edge_sum_ = 0.0;
    
    // Safety state
    bool kill_switch_armed_ = false;
    bool spread_guard_active_ = false;
    bool edge_guard_active_ = false;
};

} // namespace Bootstrap
} // namespace Chimera
