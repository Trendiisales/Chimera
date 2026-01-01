// =============================================================================
// EngineOwnership.hpp - v4.5.1 - ENGINE-LEVEL SYMBOL OWNERSHIP ENFORCEMENT
// =============================================================================
// PURPOSE: Prevent symbol leakage between engines by enforcing strict ownership
//
// v4.5.1 ADDITIONS:
//   - TIME-BASED NAS100 OWNERSHIP: IncomeEngine vs CFDEngine
//   - canTradeNAS100() execution guard (non-negotiable)
//   - Forced flat logic before income window
//   - Dashboard state tracking (owner, countdown, forced-flat timer)
//
// DESIGN PRINCIPLES:
//   1. DENY-BY-DEFAULT: If no explicit ownership exists → BLOCK
//   2. MODE-AWARE ENFORCEMENT:
//      - Demo/Shadow: Log + block (visibility during testing)
//      - Live: Throw/abort (guarantees during live trading)
//   3. SINGLE SOURCE OF TRUTH: All ownership defined here, not scattered
//   4. TIME-BASED NAS100 OWNERSHIP:
//      - IncomeEngine owns NAS100 during income window (03:00-05:00 NY)
//      - CFDEngine owns NAS100 outside income window
//      - NEVER concurrent ownership
//
// NAS100 OWNERSHIP SCHEDULE (NY TIME):
//   18:00-02:00 (Asia):       Income OFF, CFD ON (small size, ranges ok)
//   02:00-03:00 (London prep): Income OBSERVE, CFD wind-down (no new entries)
//   03:00-05:00 (Income window): Income EXCLUSIVE, CFD HARD OFF
//   05:00-10:00 (Post-income):  Income LOCKED, CFD ON (momentum/breakouts)
//
// USAGE:
//   if (!Chimera::canTradeNAS100(EngineId::CFD, nowNY)) return; // HARD STOP
// =============================================================================
#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <mutex>
#include <ctime>

namespace Chimera {

// =============================================================================
// Engine IDs - Every engine must have a unique ID
// =============================================================================
enum class EngineId : uint8_t {
    UNKNOWN = 0,
    BINANCE = 1,      // Crypto engine (Binance WebSocket)
    CFD = 2,          // CFD engine (cTrader FIX) - soldier (NAS100 outside income window)
    INCOME = 3,       // Income engine (sniper - NAS100 during income window ONLY)
    SHADOW = 4,       // Shadow execution (for bootstrapping)
    MAX_ENGINE = 5
};

inline const char* engine_id_str(EngineId id) {
    switch (id) {
        case EngineId::BINANCE:   return "BINANCE";
        case EngineId::CFD:       return "CFD";
        case EngineId::INCOME:    return "INCOME";
        case EngineId::SHADOW:    return "SHADOW";
        default:                  return "UNKNOWN";
    }
}

// =============================================================================
// Enforcement Mode - Controls behavior on ownership violation
// =============================================================================
enum class EnforcementMode : uint8_t {
    DEMO,   // Log + block, continue execution (for testing/shadow)
    LIVE    // Throw exception / abort (for production live trading)
};

// =============================================================================
// NAS100 Owner State (for dashboard display)
// =============================================================================
enum class NAS100Owner : uint8_t {
    NONE = 0,
    INCOME = 1,
    CFD = 2
};

inline const char* nas100_owner_str(NAS100Owner owner) {
    switch (owner) {
        case NAS100Owner::INCOME: return "INCOME";
        case NAS100Owner::CFD:    return "CFD";
        default:                  return "NONE";
    }
}

// =============================================================================
// NAS100 Ownership State (for GUI/dashboard)
// =============================================================================
struct NAS100OwnershipState {
    NAS100Owner current_owner = NAS100Owner::NONE;
    int seconds_to_income_window = 0;      // Countdown to income window start
    int seconds_in_income_window = 0;      // Time remaining in income window
    int cfd_forced_flat_seconds = 0;       // Seconds until CFD must flat NAS100
    bool cfd_no_new_entries = false;       // CFD blocked from new NAS100 entries
    bool income_window_active = false;
    int ny_hour = 0;
    int ny_minute = 0;
    
    void print() const {
        printf("[NAS100-OWNERSHIP] Owner=%s income_window=%s NY_time=%02d:%02d\n",
               nas100_owner_str(current_owner),
               income_window_active ? "ACTIVE" : "inactive",
               ny_hour, ny_minute);
        if (cfd_no_new_entries) {
            printf("[NAS100-OWNERSHIP] CFD: NO NEW ENTRIES (forced_flat_in=%ds)\n", cfd_forced_flat_seconds);
        }
        if (income_window_active) {
            printf("[NAS100-OWNERSHIP] Income window: %ds remaining\n", seconds_in_income_window);
        } else if (seconds_to_income_window > 0) {
            printf("[NAS100-OWNERSHIP] Income window in: %ds\n", seconds_to_income_window);
        }
    }
};

// =============================================================================
// NY Time Helper Functions
// =============================================================================

// Get current NY time (handles DST via environment)
inline std::tm getNYTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    
    // Set timezone to NY
    // Note: In production, use proper timezone library (Howard Hinnant date, or ICU)
    // For now, assume server is in UTC and NY is UTC-5 (EST) or UTC-4 (EDT)
    // This is a simplification - real implementation should use proper TZ handling
    
    std::tm utc_tm;
#ifdef _WIN32
    gmtime_s(&utc_tm, &now_t);
#else
    gmtime_r(&now_t, &utc_tm);
#endif
    
    // Simple DST check: March 2nd Sunday to November 1st Sunday (approximate)
    // More robust: use tzdb or boost::date_time
    bool is_dst = false;
    int month = utc_tm.tm_mon + 1;  // 1-12
    if (month > 3 && month < 11) {
        is_dst = true;
    } else if (month == 3) {
        // After 2nd Sunday of March
        int day = utc_tm.tm_mday;
        int second_sunday = 8 + (7 - ((8 + 6 - 1) % 7));  // Approximate
        is_dst = (day >= second_sunday);
    } else if (month == 11) {
        // Before 1st Sunday of November
        int day = utc_tm.tm_mday;
        int first_sunday = 1 + (7 - utc_tm.tm_wday);
        is_dst = (day < first_sunday);
    }
    
    int ny_offset_hours = is_dst ? -4 : -5;
    
    std::time_t ny_t = now_t + (ny_offset_hours * 3600);
    std::tm ny_tm;
#ifdef _WIN32
    gmtime_s(&ny_tm, &ny_t);
#else
    gmtime_r(&ny_t, &ny_tm);
#endif
    
    return ny_tm;
}

inline int getNYHour() {
    return getNYTime().tm_hour;
}

inline int getNYMinute() {
    return getNYTime().tm_min;
}

// =============================================================================
// NAS100 Income Window Configuration
// =============================================================================
struct IncomeWindowConfig {
    // Income window: 03:00-05:00 NY (2 hours)
    int start_hour = 3;
    int end_hour = 5;
    
    // CFD wind-down before income window
    int cfd_no_new_entries_minutes = 10;  // T-10 min: no new CFD NAS entries
    int cfd_forced_flat_minutes = 5;       // T-5 min: force flat CFD NAS positions
    
    // Income engine locks after exit
    bool income_locks_after_exit = true;
};

// =============================================================================
// Engine Ownership Manager (Singleton)
// =============================================================================
class EngineOwnership {
public:
    // =========================================================================
    // SINGLETON ACCESS
    // =========================================================================
    static EngineOwnership& instance() {
        static EngineOwnership inst;
        return inst;
    }
    
    // =========================================================================
    // ENFORCEMENT MODE CONFIGURATION
    // =========================================================================
    
    void setEnforcementMode(EnforcementMode mode) {
        enforcement_mode_ = mode;
        printf("[ENGINE-OWNERSHIP] Enforcement mode set to: %s\n",
               mode == EnforcementMode::LIVE ? "LIVE (fatal on violation)" : "DEMO (log + block)");
    }
    
    EnforcementMode getEnforcementMode() const noexcept {
        return enforcement_mode_;
    }
    
    bool isLiveMode() const noexcept {
        return enforcement_mode_ == EnforcementMode::LIVE;
    }
    
    // =========================================================================
    // INCOME WINDOW CONFIGURATION
    // =========================================================================
    
    void setIncomeWindowConfig(const IncomeWindowConfig& cfg) {
        income_cfg_ = cfg;
        printf("[ENGINE-OWNERSHIP] Income window: %02d:00-%02d:00 NY\n",
               income_cfg_.start_hour, income_cfg_.end_hour);
        printf("[ENGINE-OWNERSHIP] CFD wind-down: T-%d min no entries, T-%d min force flat\n",
               income_cfg_.cfd_no_new_entries_minutes, income_cfg_.cfd_forced_flat_minutes);
    }
    
    const IncomeWindowConfig& getIncomeWindowConfig() const noexcept {
        return income_cfg_;
    }
    
    // =========================================================================
    // NAS100 TIME-BASED OWNERSHIP (THE CRITICAL FUNCTION)
    // =========================================================================
    
    // Check if income window is currently active
    [[nodiscard]] bool isIncomeWindowActive() const noexcept {
        int h = getNYHour();
        return (h >= income_cfg_.start_hour && h < income_cfg_.end_hour);
    }
    
    // Check if CFD should stop new NAS100 entries (T-10 minutes)
    [[nodiscard]] bool isCFDNAS100WindDown() const noexcept {
        std::tm ny = getNYTime();
        int h = ny.tm_hour;
        int m = ny.tm_min;
        
        // Wind-down starts X minutes before income window
        // If income window starts at 03:00 and wind-down is 10 min:
        // Wind-down starts at 02:50
        if (h == income_cfg_.start_hour - 1) {
            int wind_down_start = 60 - income_cfg_.cfd_no_new_entries_minutes;
            return (m >= wind_down_start);
        }
        
        // Also blocked during income window
        return isIncomeWindowActive();
    }
    
    // Check if CFD must force-flat NAS100 positions (T-5 minutes)
    [[nodiscard]] bool isCFDNAS100ForcedFlat() const noexcept {
        std::tm ny = getNYTime();
        int h = ny.tm_hour;
        int m = ny.tm_min;
        
        // Forced flat starts X minutes before income window
        if (h == income_cfg_.start_hour - 1) {
            int forced_flat_start = 60 - income_cfg_.cfd_forced_flat_minutes;
            return (m >= forced_flat_start);
        }
        
        // Also forced flat during income window
        return isIncomeWindowActive();
    }
    
    // =========================================================================
    // THE NON-NEGOTIABLE EXECUTION GUARD
    // Put this inside submitOrder() - makes mistakes IMPOSSIBLE
    // =========================================================================
    [[nodiscard]] bool canTradeNAS100(EngineId engine) const noexcept {
        switch (engine) {
            case EngineId::INCOME:
                // Income engine can ONLY trade NAS100 during income window
                // AND only if not locked after exit
                return isIncomeWindowActive() && !income_locked_after_exit_.load();
                
            case EngineId::CFD:
                // CFD engine can trade NAS100 ONLY outside income window
                // AND not during wind-down period
                return !isCFDNAS100WindDown();
                
            default:
                // No other engine can trade NAS100
                return false;
        }
    }
    
    // Get current NAS100 owner
    [[nodiscard]] NAS100Owner getNAS100Owner() const noexcept {
        if (isIncomeWindowActive()) {
            return NAS100Owner::INCOME;
        }
        if (!isCFDNAS100WindDown()) {
            return NAS100Owner::CFD;
        }
        return NAS100Owner::NONE;  // Wind-down period - no owner
    }
    
    // =========================================================================
    // v4.6.0: INDEX CFD EXECUTION GUARD (US30, SPX500)
    // These are NY-session only, CFDEngine only symbols
    // =========================================================================
    [[nodiscard]] bool canTradeIndexCFD(EngineId engine, const std::string& symbol) const noexcept {
        // Only CFD engine can trade index CFDs
        if (engine != EngineId::CFD) return false;
        
        // Check if this is an index CFD
        if (symbol != "US30" && symbol != "SPX500") {
            return true;  // Not an index CFD, use normal ownership
        }
        
        // Must be NY session (09:30-16:00 NY for full session)
        // For pre-market/after-hours, use 04:00-20:00
        int h = getNYHour();
        
        // Conservative: Only allow 09:00-17:00 NY for index CFDs
        // This covers market open (09:30) with some buffer
        if (h < 9 || h >= 17) return false;
        
        // GlobalRiskGovernor check is done elsewhere
        return true;
    }
    
    // =========================================================================
    // v4.6.0: Check NY Session (04:00-20:00 NY)
    // =========================================================================
    [[nodiscard]] bool isNYSession() const noexcept {
        int h = getNYHour();
        return (h >= 4 && h < 20);
    }
    
    // Check NY Open (09:30-11:30 NY - first 2 hours)
    [[nodiscard]] bool isNYOpen() const noexcept {
        std::tm ny = getNYTime();
        int h = ny.tm_hour;
        int m = ny.tm_min;
        
        // 09:30-11:30
        if (h == 9 && m >= 30) return true;
        if (h == 10) return true;
        if (h == 11 && m < 30) return true;
        return false;
    }
    
    // Check NY Continuation (11:30-16:00 NY)
    [[nodiscard]] bool isNYContinuation() const noexcept {
        std::tm ny = getNYTime();
        int h = ny.tm_hour;
        int m = ny.tm_min;
        
        // 11:30-16:00
        if (h == 11 && m >= 30) return true;
        if (h >= 12 && h < 16) return true;
        return false;
    }
    
    // Get full ownership state for dashboard
    [[nodiscard]] NAS100OwnershipState getNAS100OwnershipState() const {
        NAS100OwnershipState state;
        std::tm ny = getNYTime();
        
        state.ny_hour = ny.tm_hour;
        state.ny_minute = ny.tm_min;
        state.current_owner = getNAS100Owner();
        state.income_window_active = isIncomeWindowActive();
        state.cfd_no_new_entries = isCFDNAS100WindDown();
        
        // Calculate seconds to income window
        if (!state.income_window_active) {
            int current_minutes = ny.tm_hour * 60 + ny.tm_min;
            int income_start_minutes = income_cfg_.start_hour * 60;
            
            if (current_minutes < income_start_minutes) {
                state.seconds_to_income_window = (income_start_minutes - current_minutes) * 60 - ny.tm_sec;
            } else {
                // After income window today, calculate to next day
                int minutes_left_today = 24 * 60 - current_minutes;
                state.seconds_to_income_window = (minutes_left_today + income_start_minutes) * 60 - ny.tm_sec;
            }
        }
        
        // Calculate seconds remaining in income window
        if (state.income_window_active) {
            int current_minutes = ny.tm_hour * 60 + ny.tm_min;
            int income_end_minutes = income_cfg_.end_hour * 60;
            state.seconds_in_income_window = (income_end_minutes - current_minutes) * 60 - ny.tm_sec;
        }
        
        // Calculate CFD forced flat countdown
        if (state.cfd_no_new_entries && !state.income_window_active) {
            int current_minutes = ny.tm_hour * 60 + ny.tm_min;
            int forced_flat_minutes = income_cfg_.start_hour * 60 - income_cfg_.cfd_forced_flat_minutes;
            if (current_minutes < forced_flat_minutes) {
                state.cfd_forced_flat_seconds = (forced_flat_minutes - current_minutes) * 60 - ny.tm_sec;
            }
        }
        
        return state;
    }
    
    // =========================================================================
    // INCOME ENGINE LOCK (after exit, engine locks for the day)
    // =========================================================================
    
    void lockIncomeEngine() {
        income_locked_after_exit_.store(true);
        printf("[ENGINE-OWNERSHIP] Income engine LOCKED (post-exit)\n");
    }
    
    void unlockIncomeEngine() {
        income_locked_after_exit_.store(false);
        printf("[ENGINE-OWNERSHIP] Income engine UNLOCKED\n");
    }
    
    bool isIncomeLocked() const noexcept {
        return income_locked_after_exit_.load();
    }
    
    // Reset lock (call at session start)
    void resetDailyState() {
        income_locked_after_exit_.store(false);
        violation_count_.store(0);
        printf("[ENGINE-OWNERSHIP] Daily state reset\n");
    }
    
    // =========================================================================
    // SYMBOL OWNERSHIP CONFIGURATION (non-NAS100)
    // =========================================================================
    
    void setAllowedSymbols(EngineId engine, const std::unordered_set<std::string>& symbols) {
        std::lock_guard<std::mutex> lock(mutex_);
        allowed_[engine] = symbols;
        
        printf("[ENGINE-OWNERSHIP] %s allowed symbols set: ", engine_id_str(engine));
        for (const auto& s : symbols) printf("%s ", s.c_str());
        printf("\n");
    }
    
    void addAllowedSymbol(EngineId engine, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        allowed_[engine].insert(symbol);
        printf("[ENGINE-OWNERSHIP] %s +%s\n", engine_id_str(engine), symbol.c_str());
    }
    
    void removeAllowedSymbol(EngineId engine, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        allowed_[engine].erase(symbol);
        printf("[ENGINE-OWNERSHIP] %s -%s\n", engine_id_str(engine), symbol.c_str());
    }
    
    void blockSymbolGlobally(const std::string& symbol, const char* reason = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        global_blocked_.insert(symbol);
        printf("[ENGINE-OWNERSHIP] GLOBAL BLOCK: %s reason=%s\n", symbol.c_str(), reason);
    }
    
    void unblockSymbolGlobally(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        global_blocked_.erase(symbol);
        printf("[ENGINE-OWNERSHIP] GLOBAL UNBLOCK: %s\n", symbol.c_str());
    }
    
    // =========================================================================
    // RUNTIME CHECKS (hot path - must be fast)
    // =========================================================================
    
    [[nodiscard]] bool isAllowed(EngineId engine, const std::string& symbol) const noexcept {
        // SPECIAL CASE: NAS100 has time-based ownership
        if (symbol == "NAS100") {
            return canTradeNAS100(engine);
        }
        
        // DENY: Global block check
        if (global_blocked_.count(symbol) > 0) {
            return false;
        }
        
        // DENY: Unknown engine
        if (engine == EngineId::UNKNOWN) {
            return false;
        }
        
        // DENY-BY-DEFAULT: Engine not configured = nothing allowed
        auto it = allowed_.find(engine);
        if (it == allowed_.end()) {
            return false;
        }
        
        // DENY-BY-DEFAULT: Symbol not in allowed list = blocked
        return it->second.count(symbol) > 0;
    }
    
    [[nodiscard]] bool isAllowedWithLog(EngineId engine, const std::string& symbol) const {
        bool allowed = isAllowed(engine, symbol);
        
        if (!allowed) {
            violation_count_.fetch_add(1, std::memory_order_relaxed);
            
            // Rate-limited logging
            std::string key = std::string(engine_id_str(engine)) + ":" + symbol;
            auto now = std::chrono::steady_clock::now();
            
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                auto it = last_violation_log_.find(key);
                if (it == last_violation_log_.end() || 
                    (now - it->second) > std::chrono::seconds(1)) {
                    last_violation_log_[key] = now;
                    
                    if (symbol == "NAS100") {
                        auto state = getNAS100OwnershipState();
                        printf("[ENGINE-OWNERSHIP] NAS100 BLOCKED: engine=%s owner=%s income_window=%s NY=%02d:%02d\n",
                               engine_id_str(engine), nas100_owner_str(state.current_owner),
                               state.income_window_active ? "ACTIVE" : "inactive",
                               state.ny_hour, state.ny_minute);
                    } else {
                        printf("[ENGINE-OWNERSHIP] BLOCKED: engine=%s symbol=%s\n",
                               engine_id_str(engine), symbol.c_str());
                    }
                }
            }
        }
        
        return allowed;
    }
    
    // =========================================================================
    // HARD ENFORCEMENT (use at execution boundary)
    // =========================================================================
    
    [[nodiscard]] bool assertAllowed(EngineId engine, const std::string& symbol) const {
        if (isAllowed(engine, symbol)) {
            return true;
        }
        
        violation_count_.fetch_add(1, std::memory_order_relaxed);
        
        const char* reason = "not in allowed list";
        if (symbol == "NAS100") {
            reason = canTradeNAS100(engine) ? "unknown" : "ownership (time-based)";
        } else if (global_blocked_.count(symbol) > 0) {
            reason = "globally blocked";
        } else if (allowed_.find(engine) == allowed_.end()) {
            reason = "engine has no ownership config (DENY-BY-DEFAULT)";
        }
        
        char buf[256];
        snprintf(buf, sizeof(buf), 
            "[ENGINE-OWNERSHIP] VIOLATION: engine=%s symbol=%s reason=%s",
            engine_id_str(engine), symbol.c_str(), reason);
        
        if (enforcement_mode_ == EnforcementMode::LIVE) {
            fprintf(stderr, "%s [FATAL - ABORTING]\n", buf);
            throw std::runtime_error(buf);
        } else {
            fprintf(stderr, "%s [BLOCKED]\n", buf);
            return false;
        }
    }
    
    void assertAllowedOrAbort(EngineId engine, const std::string& symbol) const {
        if (!isAllowed(engine, symbol)) {
            fprintf(stderr, 
                "[ENGINE-OWNERSHIP] FATAL VIOLATION: engine=%s attempted forbidden symbol=%s\n"
                "[ENGINE-OWNERSHIP] This is a critical invariant violation. ABORTING.\n",
                engine_id_str(engine), symbol.c_str());
            std::abort();
        }
    }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    
    uint64_t getViolationCount() const noexcept {
        return violation_count_.load(std::memory_order_relaxed);
    }
    
    void resetViolationCount() noexcept {
        violation_count_.store(0, std::memory_order_relaxed);
    }
    
    bool hasOwnershipConfig(EngineId engine) const {
        return allowed_.find(engine) != allowed_.end();
    }
    
    std::unordered_set<std::string> getOwnedSymbols(EngineId engine) const {
        auto it = allowed_.find(engine);
        if (it != allowed_.end()) {
            return it->second;
        }
        return {};
    }
    
    void printConfig() const {
        printf("[ENGINE-OWNERSHIP] Current Configuration:\n");
        printf("  Enforcement mode: %s\n", 
               enforcement_mode_ == EnforcementMode::LIVE ? "LIVE (fatal)" : "DEMO (log+block)");
        printf("  Policy: DENY-BY-DEFAULT (unconfigured engine+symbol = BLOCKED)\n");
        printf("  NAS100 ownership: TIME-BASED\n");
        printf("    Income window: %02d:00-%02d:00 NY\n", income_cfg_.start_hour, income_cfg_.end_hour);
        printf("    CFD wind-down: T-%d min (no entries), T-%d min (force flat)\n",
               income_cfg_.cfd_no_new_entries_minutes, income_cfg_.cfd_forced_flat_minutes);
        
        for (const auto& [engine, symbols] : allowed_) {
            printf("  %s: ", engine_id_str(engine));
            for (const auto& s : symbols) {
                if (s != "NAS100") printf("%s ", s.c_str());  // NAS100 handled separately
            }
            printf("\n");
        }
        if (!global_blocked_.empty()) {
            printf("  GLOBAL_BLOCKED: ");
            for (const auto& s : global_blocked_) printf("%s ", s.c_str());
            printf("\n");
        }
        
        // Current NAS100 state
        auto state = getNAS100OwnershipState();
        printf("  NAS100 current owner: %s (NY time: %02d:%02d)\n",
               nas100_owner_str(state.current_owner), state.ny_hour, state.ny_minute);
        
        printf("  Violation count: %lu\n", violation_count_.load());
    }
    
private:
    EngineOwnership() {
        enforcement_mode_ = EnforcementMode::DEMO;
        
        // =====================================================================
        // DEFAULT OWNERSHIP CONFIGURATION (v4.5.1)
        // NAS100 is TIME-BASED - not in static lists
        // =====================================================================
        
        // INCOME ENGINE: NAS100 ONLY (time-based, not in this list)
        // Other symbols NOT allowed for income
        allowed_[EngineId::INCOME] = {};  // Empty - only NAS100 via time-based
        
        // CFD ENGINE: All CFD symbols
        // NAS100 is time-based, but we include it for non-time-based checks
        allowed_[EngineId::CFD] = {
            // Metals
            "XAUUSD", "XAGUSD",
            // Indices (NAS100 is time-based, but listed for reference)
            "US30", "US100", "SPX500", "GER40", "UK100",
            // FX Majors
            "EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD", "NZDUSD", "USDCHF",
            // FX Crosses
            "EURGBP", "AUDNZD"
        };
        
        // BINANCE ENGINE: Crypto only
        allowed_[EngineId::BINANCE] = {
            "BTCUSDT", "ETHUSDT"
        };
        
        // SHADOW ENGINE: Same as CFD
        allowed_[EngineId::SHADOW] = allowed_[EngineId::CFD];
        
        printf("[ENGINE-OWNERSHIP] Initialized with DENY-BY-DEFAULT + TIME-BASED NAS100 ownership\n");
        printConfig();
    }
    
    EnforcementMode enforcement_mode_;
    IncomeWindowConfig income_cfg_;
    std::unordered_map<EngineId, std::unordered_set<std::string>> allowed_;
    std::unordered_set<std::string> global_blocked_;
    mutable std::atomic<uint64_t> violation_count_{0};
    mutable std::atomic<bool> income_locked_after_exit_{false};
    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_violation_log_;
    mutable std::mutex mutex_;
    mutable std::mutex log_mutex_;
};

// =============================================================================
// CONVENIENCE FUNCTIONS (for cleaner call sites)
// =============================================================================

inline bool isEngineAllowed(EngineId engine, const std::string& symbol) {
    return EngineOwnership::instance().isAllowed(engine, symbol);
}

inline bool assertEngineAllowed(EngineId engine, const std::string& symbol) {
    return EngineOwnership::instance().assertAllowed(engine, symbol);
}

inline void assertEngineAllowedOrAbort(EngineId engine, const std::string& symbol) {
    EngineOwnership::instance().assertAllowedOrAbort(engine, symbol);
}

// =============================================================================
// NAS100-SPECIFIC CONVENIENCE FUNCTIONS
// =============================================================================

// THE CRITICAL GUARD - Use this inside submitOrder() for NAS100
inline bool canTradeNAS100(EngineId engine) {
    return EngineOwnership::instance().canTradeNAS100(engine);
}

inline bool isIncomeWindowActive() {
    return EngineOwnership::instance().isIncomeWindowActive();
}

inline bool isCFDNAS100WindDown() {
    return EngineOwnership::instance().isCFDNAS100WindDown();
}

inline bool isCFDNAS100ForcedFlat() {
    return EngineOwnership::instance().isCFDNAS100ForcedFlat();
}

inline NAS100Owner getNAS100Owner() {
    return EngineOwnership::instance().getNAS100Owner();
}

inline NAS100OwnershipState getNAS100OwnershipState() {
    return EngineOwnership::instance().getNAS100OwnershipState();
}

// =============================================================================
// v4.6.0: INDEX CFD CONVENIENCE FUNCTIONS (US30, SPX500)
// =============================================================================

inline bool canTradeIndexCFD(EngineId engine, const std::string& symbol) {
    return EngineOwnership::instance().canTradeIndexCFD(engine, symbol);
}

inline bool isNYSession() {
    return EngineOwnership::instance().isNYSession();
}

inline bool isNYOpen() {
    return EngineOwnership::instance().isNYOpen();
}

inline bool isNYContinuation() {
    return EngineOwnership::instance().isNYContinuation();
}

// =============================================================================
// TRADE ATTRIBUTION (logs engine_id with every trade)
// =============================================================================
struct TradeAttribution {
    EngineId engine_id = EngineId::UNKNOWN;
    char symbol[16] = {0};
    char strategy[32] = {0};
    int8_t direction = 0;
    double size = 0.0;
    double price = 0.0;
    double pnl = 0.0;
    uint64_t timestamp_ns = 0;
    
    void print() const {
        printf("[TRADE] engine=%s strategy=%s symbol=%s dir=%d size=%.4f price=%.5f pnl=%.2f\n",
               engine_id_str(engine_id), strategy, symbol, direction, size, price, pnl);
    }
};

} // namespace Chimera
