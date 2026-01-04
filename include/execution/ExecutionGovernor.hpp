// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ExecutionGovernor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.27
// PURPOSE: Venue health monitoring, auto-failover, and FEEDBACK LOOP PREVENTION
// OWNER: Jo
// CREATED: 2026-01-03
//
// v4.9.27 PHASE 3 FIXES:
// ═══════════════════════════════════════════════════════════════════════════════
//   FIX 3.4: ALARM DAMPENING
//     - No audible alarms unless state persists > 3 seconds
//     - Prevents alarm storms from SSL blips, reconnects, probe backoffs
//     - CONNECTION_LOST alarm only fires after 3s of sustained loss
//
//   FIX 3.1: HARD PROBE FREEZE DURING HALT (marker only - impl in MicroScalp)
//     - ExecutionGovernor.isHalted() → no probes
//
// v4.9.25 FIXES (FEEDBACK LOOP ELIMINATION):
//   FIX #1: canSubmit() now hard-gates ALL order submission
//   FIX #3: Alarm latches prevent repeated alarms for same condition
//   FIX #4: Per-symbol FROZEN state with backoff timers
//   FIX #5: Venue halt does NOT degrade alpha/expectancy
//
// This is LAYER B of the adaptive stack:
//   If a venue degrades, Chimera stops trusting it automatically.
//
// STATE MACHINE:
//   HEALTHY → DEGRADED → HALTED → RECOVERY_COOLDOWN → HEALTHY
//   
//   Conditions:
//     1 timeout/error  → DEGRADED (restrict symbols, widen filters)
//     2 issues         → DEGRADED (further restrictions)
//     3+ issues        → HALTED (stop all trading)
//     Connection lost  → HALTED (immediate)
//     Connection restored → RECOVERY_COOLDOWN (5s) → HEALTHY
//
// ALARM LATCH SYSTEM (FIX #3):
//   - Each condition type has an alarm latch
//   - Alarm fires ONCE on state TRANSITION
//   - No repeated alarms for persistent conditions
//   - Recovery alarm fires when condition clears
//
// v4.9.27 ALARM DAMPENING:
//   - State must persist for ALARM_DAMPENING_NS (3s) before alarm fires
//   - Prevents false alarms from transient network events
//   - Still logs state transitions immediately
//
// DESIGN:
//   - Per-venue state tracking (currently Binance only)
//   - Cooldown timers for recovery
//   - Integration with ExecutionAuthority
//   - Per-symbol freeze with backoff
//   - No ML - pure threshold rules
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <string>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Venue Health State
// ─────────────────────────────────────────────────────────────────────────────
enum class VenueState : uint8_t {
    HEALTHY           = 0,   // All systems nominal
    DEGRADED          = 1,   // Issues detected, trading restricted
    HALTED            = 2,   // Critical issues, no trading
    RECOVERY_COOLDOWN = 3    // Connection restored, waiting for stability
};

inline const char* venueStateToString(VenueState s) {
    switch (s) {
        case VenueState::HEALTHY:           return "HEALTHY";
        case VenueState::DEGRADED:          return "DEGRADED";
        case VenueState::HALTED:            return "HALTED";
        case VenueState::RECOVERY_COOLDOWN: return "RECOVERY_COOLDOWN";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Degradation Reason (for diagnostics)
// ─────────────────────────────────────────────────────────────────────────────
enum class DegradationReason : uint8_t {
    NONE            = 0,
    ACK_TIMEOUT     = 1,
    ORDER_ERROR     = 2,
    CONNECTION_LOST = 3,
    HIGH_LATENCY    = 4,
    HIGH_REJECT_RATE= 5,
    RATE_LIMITED    = 6,
    PROBE_TIMEOUT   = 7,   // v4.9.25: Probe-specific timeout
    SIGNATURE_ERROR = 8    // v4.9.27: Signature rejection (-1022)
};

inline const char* degradationReasonToString(DegradationReason r) {
    switch (r) {
        case DegradationReason::NONE:            return "NONE";
        case DegradationReason::ACK_TIMEOUT:     return "ACK_TIMEOUT";
        case DegradationReason::ORDER_ERROR:     return "ORDER_ERROR";
        case DegradationReason::CONNECTION_LOST: return "CONNECTION_LOST";
        case DegradationReason::HIGH_LATENCY:    return "HIGH_LATENCY";
        case DegradationReason::HIGH_REJECT_RATE:return "HIGH_REJECT_RATE";
        case DegradationReason::RATE_LIMITED:    return "RATE_LIMITED";
        case DegradationReason::PROBE_TIMEOUT:   return "PROBE_TIMEOUT";
        case DegradationReason::SIGNATURE_ERROR: return "SIGNATURE_ERROR";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alarm Latch (FIX #3) - Prevents repeated alarms for same condition
// v4.9.27: Added dampening timestamp tracking
// ─────────────────────────────────────────────────────────────────────────────
struct AlarmLatch {
    std::atomic<bool> connection_lost_active{false};
    std::atomic<bool> venue_halted_active{false};
    std::atomic<bool> venue_degraded_active{false};
    std::atomic<bool> high_latency_active{false};
    std::atomic<bool> rate_limited_active{false};
    
    // v4.9.27: Dampening timestamps
    std::atomic<uint64_t> connection_lost_start_ns{0};
    std::atomic<uint64_t> venue_halted_start_ns{0};
    std::atomic<bool> connection_lost_alarm_fired{false};
    std::atomic<bool> venue_halted_alarm_fired{false};
    
    // Fire alarm only on state TRANSITION (edge-triggered)
    // Returns true if alarm should fire (state transition occurred)
    bool shouldFireConnectionLost() {
        bool expected = false;
        return connection_lost_active.compare_exchange_strong(expected, true);
    }
    
    bool shouldFireConnectionRestored() {
        bool expected = true;
        return connection_lost_active.compare_exchange_strong(expected, false);
    }
    
    bool shouldFireVenueHalted() {
        bool expected = false;
        return venue_halted_active.compare_exchange_strong(expected, true);
    }
    
    bool shouldFireVenueRecovered() {
        bool expected = true;
        return venue_halted_active.compare_exchange_strong(expected, false);
    }
    
    bool shouldFireVenueDegraded() {
        bool expected = false;
        return venue_degraded_active.compare_exchange_strong(expected, true);
    }
    
    bool shouldFireDegradedCleared() {
        bool expected = true;
        return venue_degraded_active.compare_exchange_strong(expected, false);
    }
    
    void reset() {
        connection_lost_active.store(false);
        venue_halted_active.store(false);
        venue_degraded_active.store(false);
        high_latency_active.store(false);
        rate_limited_active.store(false);
        connection_lost_start_ns.store(0);
        venue_halted_start_ns.store(0);
        connection_lost_alarm_fired.store(false);
        venue_halted_alarm_fired.store(false);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Freeze State (FIX #4)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolFreezeState {
    std::atomic<bool> frozen{false};
    std::atomic<uint64_t> freeze_until_ns{0};
    std::atomic<uint32_t> consecutive_probe_timeouts{0};
    std::atomic<uint64_t> last_probe_timeout_ns{0};
    
    static constexpr uint64_t BASE_FREEZE_NS = 1'000'000'000ULL;      // 1 second
    static constexpr uint64_t MAX_FREEZE_NS = 30'000'000'000ULL;      // 30 seconds
    static constexpr uint32_t TIMEOUT_THRESHOLD_TO_FREEZE = 2;        // 2 timeouts → freeze
    
    bool isFrozen(uint64_t now_ns) const {
        if (!frozen.load(std::memory_order_acquire)) return false;
        return now_ns < freeze_until_ns.load(std::memory_order_acquire);
    }
    
    void recordProbeTimeout(uint64_t now_ns) {
        uint32_t count = consecutive_probe_timeouts.fetch_add(1) + 1;
        last_probe_timeout_ns.store(now_ns, std::memory_order_release);
        
        if (count >= TIMEOUT_THRESHOLD_TO_FREEZE) {
            // Exponential backoff: 1s, 2s, 4s, 8s, ... up to 30s
            uint64_t backoff_mult = 1ULL << std::min(count - TIMEOUT_THRESHOLD_TO_FREEZE, 4U);
            uint64_t freeze_duration = std::min(BASE_FREEZE_NS * backoff_mult, MAX_FREEZE_NS);
            freeze_until_ns.store(now_ns + freeze_duration, std::memory_order_release);
            frozen.store(true, std::memory_order_release);
        }
    }
    
    void recordSuccess() {
        consecutive_probe_timeouts.store(0, std::memory_order_release);
        frozen.store(false, std::memory_order_release);
    }
    
    void clearFreeze() {
        frozen.store(false, std::memory_order_release);
        freeze_until_ns.store(0, std::memory_order_release);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Execution Governor (Singleton)
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionGovernor {
public:
    static ExecutionGovernor& instance() {
        static ExecutionGovernor g;
        return g;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // FIX #1: HARD GATE - THE ONLY FUNCTION STRATEGIES NEED TO CALL
    // ═══════════════════════════════════════════════════════════════════════
    // Returns true ONLY if:
    //   1. Venue is HEALTHY or DEGRADED (not HALTED or RECOVERY_COOLDOWN)
    //   2. Symbol is not FROZEN (probe backoff)
    //   3. Connection is alive
    // 
    // Usage: At the TOP of any strategy's checkEntryFilters():
    //   if (!ExecutionGovernor::instance().canSubmit(symbol)) return false;
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] bool canSubmit(const char* symbol) const noexcept {
        // Check venue state first (most common rejection path)
        VenueState s = state_.load(std::memory_order_acquire);
        if (s == VenueState::HALTED || s == VenueState::RECOVERY_COOLDOWN) {
            return false;
        }
        
        // Check per-symbol freeze (FIX #4)
        uint64_t now = now_ns();
        auto it = symbol_freeze_.find(symbol);
        if (it != symbol_freeze_.end() && it->second.isFrozen(now)) {
            return false;
        }
        
        return true;
    }
    
    // Convenience for logging blocked reason
    [[nodiscard]] const char* getBlockReason(const char* symbol) const noexcept {
        VenueState s = state_.load(std::memory_order_acquire);
        if (s == VenueState::HALTED) return "VENUE_HALTED";
        if (s == VenueState::RECOVERY_COOLDOWN) return "RECOVERY_COOLDOWN";
        
        auto it = symbol_freeze_.find(symbol);
        if (it != symbol_freeze_.end() && it->second.isFrozen(now_ns())) {
            return "SYMBOL_FROZEN";
        }
        
        return nullptr;  // Not blocked
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // EVENT HANDLERS (called by OrderSender)
    // ═══════════════════════════════════════════════════════════════════════
    
    void on_ack_success(const char* symbol) {
        consecutive_issues_.store(0);
        last_success_ns_ = now_ns();
        
        // Clear symbol freeze on success
        auto it = symbol_freeze_.find(symbol);
        if (it != symbol_freeze_.end()) {
            it->second.recordSuccess();
        }
        
        // Check for recovery from degraded state
        VenueState current = state_.load();
        if (current == VenueState::DEGRADED) {
            uint64_t healthy_duration = now_ns() - last_issue_ns_;
            if (healthy_duration > RECOVERY_COOLDOWN_NS) {
                transition_to(VenueState::HEALTHY, DegradationReason::NONE);
            }
        }
        // Recovery from RECOVERY_COOLDOWN is timer-based (see tick())
    }
    
    void on_ack_timeout(const char* symbol) {
        record_issue(symbol, DegradationReason::ACK_TIMEOUT);
    }
    
    void on_order_error(const char* symbol, int error_code) {
        // Some errors are critical, others are not
        if (error_code == -1022 || error_code == -1021 || error_code == -1008) {
            // Signature / timestamp / server overload - critical
            if (error_code == -1022) {
                // v4.9.27: Signature error specifically
                record_issue(symbol, DegradationReason::SIGNATURE_ERROR);
                signature_rejections_.fetch_add(1, std::memory_order_relaxed);
            } else {
                record_issue(symbol, DegradationReason::ORDER_ERROR);
            }
        } else if (error_code == -1015) {
            // Rate limited - critical
            record_issue(symbol, DegradationReason::RATE_LIMITED);
        }
        // Other errors (like -2010 insufficient balance) don't degrade venue
    }
    
    // FIX #4: Record probe timeout and potentially freeze symbol
    void on_probe_timeout(const char* symbol) {
        // Only count as venue issue if venue is healthy
        // (don't poison stats during venue halt - FIX #5)
        VenueState s = state_.load();
        if (s == VenueState::HEALTHY) {
            // This is a real probe timeout, record it
            auto& freeze = symbol_freeze_[symbol];
            freeze.recordProbeTimeout(now_ns());
            
            if (freeze.isFrozen(now_ns())) {
                // Only log ONCE when freeze starts (alarm latch principle)
                printf("\n");
                printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ [EXEC_GOV] SYMBOL FROZEN: %s                                    \n", symbol);
                printf("║ Reason: Consecutive probe timeouts                               \n");
                printf("║ Duration: %llu seconds                                           \n",
                       (unsigned long long)(freeze.freeze_until_ns.load() - now_ns()) / 1'000'000'000ULL);
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                printf("\n");
            }
        }
        // If venue is HALTED, probe timeout is expected - don't record
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.27 FIX 3.4: CONNECTION LOST WITH ALARM DAMPENING
    // ═══════════════════════════════════════════════════════════════════════
    void on_connection_lost() {
        uint64_t now = now_ns();
        
        // FIX #3: Record transition but check dampening before alarm
        if (alarm_latch_.shouldFireConnectionLost()) {
            // NEW transition - record start time
            alarm_latch_.connection_lost_start_ns.store(now, std::memory_order_release);
            alarm_latch_.connection_lost_alarm_fired.store(false, std::memory_order_release);
            
            // Log transition immediately (but no alarm yet)
            printf("[EXEC_GOV] CONNECTION_LOST detected - waiting %llums for persistence\n",
                   (unsigned long long)(ALARM_DAMPENING_NS / 1'000'000ULL));
        }
        
        record_issue("*", DegradationReason::CONNECTION_LOST);
        // Connection loss is severe - go straight to HALTED
        transition_to(VenueState::HALTED, DegradationReason::CONNECTION_LOST);
    }
    
    void on_connection_restored() {
        // FIX #3: Only fire alarm on state TRANSITION
        if (alarm_latch_.shouldFireConnectionRestored()) {
            // v4.9.27: Check if dampened alarm should fire for recovery
            if (alarm_latch_.connection_lost_alarm_fired.load()) {
                // We fired a loss alarm, so fire recovery alarm
                printf("\n");
                printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ ✓ [ALERT] BINANCE WEBSOCKET CONNECTION RESTORED                     ║\n");
                printf("║ Entering recovery cooldown (%llu seconds)                           ║\n",
                       (unsigned long long)(RECOVERY_COOLDOWN_NS / 1'000'000'000ULL));
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                printf("\n");
            } else {
                // Loss was transient, no alarm fired, just log quietly
                printf("[EXEC_GOV] Connection restored (transient loss, no alarm was fired)\n");
            }
            
            // Reset dampening state
            alarm_latch_.connection_lost_start_ns.store(0, std::memory_order_release);
            alarm_latch_.connection_lost_alarm_fired.store(false, std::memory_order_release);
        }
        
        recovery_start_ns_ = now_ns();
        transition_to(VenueState::RECOVERY_COOLDOWN, DegradationReason::NONE);
    }
    
    void on_high_latency(const char* symbol, uint64_t latency_us) {
        if (latency_us > HIGH_LATENCY_THRESHOLD_US) {
            high_latency_count_.fetch_add(1);
            if (high_latency_count_.load() >= HIGH_LATENCY_TRIGGER_COUNT) {
                record_issue(symbol, DegradationReason::HIGH_LATENCY);
                high_latency_count_.store(0);  // Reset after triggering
            }
        } else {
            // Good latency - decay the counter
            uint64_t count = high_latency_count_.load();
            if (count > 0) {
                high_latency_count_.compare_exchange_weak(count, count - 1);
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // TICK - Call periodically to check recovery timers + alarm dampening
    // ═══════════════════════════════════════════════════════════════════════
    void tick() {
        VenueState s = state_.load();
        uint64_t now = now_ns();
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.27 FIX 3.4: Check alarm dampening timers
        // ═══════════════════════════════════════════════════════════════════
        
        // Connection lost dampening
        if (alarm_latch_.connection_lost_active.load()) {
            uint64_t start = alarm_latch_.connection_lost_start_ns.load();
            if (start > 0 && !alarm_latch_.connection_lost_alarm_fired.load()) {
                uint64_t elapsed = now - start;
                if (elapsed >= ALARM_DAMPENING_NS) {
                    // Persistence threshold reached - fire alarm
                    alarm_latch_.connection_lost_alarm_fired.store(true, std::memory_order_release);
                    
                    printf("\n");
                    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                    printf("║ ⚠️  [ALERT] BINANCE WEBSOCKET CONNECTION LOST (SUSTAINED > 3s)       ║\n");
                    printf("║ Trading HALTED until connection restored                            ║\n");
                    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                    printf("\n");
                }
            }
        }
        
        // Venue halted dampening (similar pattern)
        if (alarm_latch_.venue_halted_active.load()) {
            uint64_t start = alarm_latch_.venue_halted_start_ns.load();
            if (start > 0 && !alarm_latch_.venue_halted_alarm_fired.load()) {
                uint64_t elapsed = now - start;
                if (elapsed >= ALARM_DAMPENING_NS) {
                    alarm_latch_.venue_halted_alarm_fired.store(true, std::memory_order_release);
                    
                    printf("\n");
                    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                    printf("║ ⚠️  [ALERT] VENUE HALTED (SUSTAINED > 3s)                            ║\n");
                    printf("║ Reason: %s                                                   \n",
                           degradationReasonToString(last_reason_.load()));
                    printf("║ All trading PAUSED                                                  ║\n");
                    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                    printf("\n");
                }
            }
        }
        
        // Check recovery cooldown timer
        if (s == VenueState::RECOVERY_COOLDOWN) {
            uint64_t elapsed = now - recovery_start_ns_;
            if (elapsed >= RECOVERY_COOLDOWN_NS) {
                transition_to(VenueState::HEALTHY, DegradationReason::NONE);
                consecutive_issues_.store(0);
            }
        }
        
        // Check symbol freeze timers (auto-unfreeze)
        for (auto& [sym, freeze] : symbol_freeze_) {
            if (freeze.frozen.load() && !freeze.isFrozen(now)) {
                freeze.clearFreeze();
                printf("[EXEC_GOV] Symbol %s unfrozen (timer expired)\n", sym.c_str());
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY STATE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] VenueState state() const noexcept {
        return state_.load();
    }
    
    [[nodiscard]] DegradationReason last_reason() const noexcept {
        return last_reason_.load();
    }
    
    // DEPRECATED: Use canSubmit() instead
    [[nodiscard]] bool allow_trading(const char* symbol) const noexcept {
        return canSubmit(symbol);
    }
    
    [[nodiscard]] bool is_healthy() const noexcept {
        return state_.load() == VenueState::HEALTHY;
    }
    
    [[nodiscard]] bool is_degraded() const noexcept {
        return state_.load() == VenueState::DEGRADED;
    }
    
    [[nodiscard]] bool is_halted() const noexcept {
        VenueState s = state_.load();
        return s == VenueState::HALTED || s == VenueState::RECOVERY_COOLDOWN;
    }
    
    [[nodiscard]] bool isVenueHealthy() const noexcept {
        VenueState s = state_.load();
        return s == VenueState::HEALTHY || s == VenueState::DEGRADED;
    }
    
    [[nodiscard]] uint64_t consecutive_issues() const noexcept {
        return consecutive_issues_.load();
    }
    
    [[nodiscard]] bool isSymbolFrozen(const char* symbol) const noexcept {
        auto it = symbol_freeze_.find(symbol);
        if (it == symbol_freeze_.end()) return false;
        return it->second.isFrozen(now_ns());
    }
    
    // v4.9.27: Get signature rejection count
    [[nodiscard]] uint64_t signature_rejections() const noexcept {
        return signature_rejections_.load(std::memory_order_relaxed);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MANUAL CONTROLS
    // ═══════════════════════════════════════════════════════════════════════
    
    void force_healthy() {
        printf("[EXEC_GOV] MANUAL: Forcing HEALTHY state\n");
        transition_to(VenueState::HEALTHY, DegradationReason::NONE);
        consecutive_issues_.store(0);
        alarm_latch_.reset();
        
        // Clear all symbol freezes
        for (auto& [sym, freeze] : symbol_freeze_) {
            freeze.clearFreeze();
            freeze.consecutive_probe_timeouts.store(0);
        }
    }
    
    void force_halt(const char* reason) {
        printf("[EXEC_GOV] MANUAL: Forcing HALT - %s\n", reason);
        transition_to(VenueState::HALTED, DegradationReason::NONE);
    }
    
    void unfreeze_symbol(const char* symbol) {
        auto it = symbol_freeze_.find(symbol);
        if (it != symbol_freeze_.end()) {
            it->second.clearFreeze();
            it->second.consecutive_probe_timeouts.store(0);
            printf("[EXEC_GOV] MANUAL: Unfroze symbol %s\n", symbol);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void set_thresholds(uint64_t degraded_issues, uint64_t halt_issues) {
        degraded_threshold_ = degraded_issues;
        halt_threshold_ = halt_issues;
    }
    
    // Print status
    void print_status() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ EXECUTION GOVERNOR STATUS (v4.9.27)                                  ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ State: %s\n", venueStateToString(state_.load()));
        printf("║ Last Reason: %s\n", degradationReasonToString(last_reason_.load()));
        printf("║ Consecutive Issues: %llu\n", static_cast<unsigned long long>(consecutive_issues_.load()));
        printf("║ Signature Rejections: %llu\n", static_cast<unsigned long long>(signature_rejections_.load()));
        printf("║ Trading Allowed: %s\n", canSubmit("*") ? "YES" : "NO");
        printf("║ Alarm Dampening: %llums\n", (unsigned long long)(ALARM_DAMPENING_NS / 1'000'000ULL));
        
        // Show frozen symbols
        bool has_frozen = false;
        for (const auto& [sym, freeze] : symbol_freeze_) {
            if (freeze.isFrozen(now_ns())) {
                if (!has_frozen) {
                    printf("║ Frozen Symbols:\n");
                    has_frozen = true;
                }
                uint64_t remain_s = (freeze.freeze_until_ns.load() - now_ns()) / 1'000'000'000ULL;
                printf("║   - %s (%llu seconds remaining)\n", sym.c_str(), (unsigned long long)remain_s);
            }
        }
        if (!has_frozen) {
            printf("║ Frozen Symbols: None\n");
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    ExecutionGovernor()
        : state_(VenueState::HEALTHY)
        , last_reason_(DegradationReason::NONE)
        , consecutive_issues_(0)
        , high_latency_count_(0)
        , signature_rejections_(0)
        , last_issue_ns_(0)
        , last_success_ns_(0)
        , recovery_start_ns_(0)
        , degraded_threshold_(1)
        , halt_threshold_(3)
    {}
    
    void record_issue(const char* symbol, DegradationReason reason) {
        uint64_t issues = consecutive_issues_.fetch_add(1) + 1;
        last_issue_ns_ = now_ns();
        last_reason_.store(reason);
        
        // Only log if this is a new issue count (not repeated)
        printf("[EXEC_GOV] Issue #%llu on %s: %s\n",
               static_cast<unsigned long long>(issues),
               symbol,
               degradationReasonToString(reason));
        
        // State transitions based on issue count
        if (issues >= halt_threshold_) {
            transition_to(VenueState::HALTED, reason);
        } else if (issues >= degraded_threshold_) {
            transition_to(VenueState::DEGRADED, reason);
        }
    }
    
    void transition_to(VenueState new_state, DegradationReason reason) {
        VenueState old = state_.exchange(new_state);
        uint64_t now = now_ns();
        
        if (old != new_state) {
            // v4.9.27: State transition with dampening for alarms
            if (new_state == VenueState::HALTED) {
                if (alarm_latch_.shouldFireVenueHalted()) {
                    // Record start time for dampening - alarm fires in tick() if persists
                    alarm_latch_.venue_halted_start_ns.store(now, std::memory_order_release);
                    alarm_latch_.venue_halted_alarm_fired.store(false, std::memory_order_release);
                    
                    // Log immediately but alarm is dampened
                    printf("[EXEC_GOV] STATE TRANSITION: %s → HALTED (reason: %s) - awaiting dampening\n",
                           venueStateToString(old), degradationReasonToString(reason));
                }
            } else if (new_state == VenueState::HEALTHY && 
                       (old == VenueState::HALTED || old == VenueState::RECOVERY_COOLDOWN)) {
                if (alarm_latch_.shouldFireVenueRecovered()) {
                    // Reset dampening state
                    alarm_latch_.venue_halted_start_ns.store(0, std::memory_order_release);
                    
                    if (alarm_latch_.venue_halted_alarm_fired.load()) {
                        printf("\n");
                        printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                        printf("║ [EXEC_GOV] VENUE RECOVERED: %s → HEALTHY                    \n",
                               venueStateToString(old));
                        printf("║ Trading RESUMED                                                     ║\n");
                        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                        printf("\n");
                    } else {
                        printf("[EXEC_GOV] STATE TRANSITION: %s → HEALTHY (recovered, no alarm was fired)\n",
                               venueStateToString(old));
                    }
                    alarm_latch_.venue_halted_alarm_fired.store(false, std::memory_order_release);
                }
            } else if (new_state == VenueState::DEGRADED) {
                if (alarm_latch_.shouldFireVenueDegraded()) {
                    printf("[EXEC_GOV] STATE TRANSITION: %s → DEGRADED (reason: %s)\n",
                           venueStateToString(old), degradationReasonToString(reason));
                }
            } else if (new_state == VenueState::RECOVERY_COOLDOWN) {
                printf("[EXEC_GOV] STATE TRANSITION: %s → RECOVERY_COOLDOWN\n",
                       venueStateToString(old));
            } else {
                // Other transitions - simple log
                printf("[EXEC_GOV] STATE TRANSITION: %s → %s\n",
                       venueStateToString(old), venueStateToString(new_state));
            }
        }
    }
    
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    // State
    std::atomic<VenueState> state_;
    std::atomic<DegradationReason> last_reason_;
    std::atomic<uint64_t> consecutive_issues_;
    std::atomic<uint64_t> high_latency_count_;
    std::atomic<uint64_t> signature_rejections_;  // v4.9.27
    std::atomic<uint64_t> last_issue_ns_;
    std::atomic<uint64_t> last_success_ns_;
    std::atomic<uint64_t> recovery_start_ns_;
    
    // FIX #3: Alarm latches
    mutable AlarmLatch alarm_latch_;
    
    // FIX #4: Per-symbol freeze state
    mutable std::unordered_map<std::string, SymbolFreezeState> symbol_freeze_;
    
    // Thresholds
    uint64_t degraded_threshold_;
    uint64_t halt_threshold_;
    
    // Constants
    static constexpr uint64_t RECOVERY_COOLDOWN_NS = 5'000'000'000ULL;   // 5 seconds
    static constexpr uint64_t HIGH_LATENCY_THRESHOLD_US = 500'000;       // 500ms
    static constexpr uint64_t HIGH_LATENCY_TRIGGER_COUNT = 5;            // 5 high latencies
    
    // v4.9.27: Alarm dampening threshold (3 seconds)
    static constexpr uint64_t ALARM_DAMPENING_NS = 3'000'000'000ULL;     // 3 seconds
};

} // namespace Chimera
