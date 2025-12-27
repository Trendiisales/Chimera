// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/shadow/ExpectancySnapshotLogger.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Audit trail for expectancy state changes
// OWNER: Jo
// VERSION: v3.0
//
// TRIGGERS SNAPSHOT ON:
// - State change (LIVE → PAUSE, PAUSE → LIVE, etc.)
// - Expectancy sign flip (positive → negative)
// - Session boundary crossed
// - Divergence threshold breach
//
// OUTPUT: CSV or JSONL, one line per event, timestamped, immutable
// This is your future self-defence.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <unordered_map>

namespace Chimera {
namespace Shadow {

// ═══════════════════════════════════════════════════════════════════════════════
// PAUSE REASON CODES
// No text, no buttons. Just truth in 3 letters.
// ═══════════════════════════════════════════════════════════════════════════════
enum class PauseReason : uint8_t {
    NONE = 0,
    EXP,   // Expectancy decay
    DIV,   // Live vs shadow divergence
    REG,   // Regime toxicity
    LAT,   // Latency / slippage breach
    SLP,   // Slope acceleration decay
    SES,   // Session filter
    MAN    // Manual operator action
};

inline const char* pause_reason_code(PauseReason r) noexcept {
    switch (r) {
        case PauseReason::NONE: return "";
        case PauseReason::EXP:  return "EXP";
        case PauseReason::DIV:  return "DIV";
        case PauseReason::REG:  return "REG";
        case PauseReason::LAT:  return "LAT";
        case PauseReason::SLP:  return "SLP";
        case PauseReason::SES:  return "SES";
        case PauseReason::MAN:  return "MAN";
        default: return "UNK";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// TRADING STATE
// ═══════════════════════════════════════════════════════════════════════════════
enum class TradingState : uint8_t {
    OFF = 0,
    SHADOW,
    WARN,
    LIVE,
    PAUSE
};

inline const char* state_name(TradingState s) noexcept {
    switch (s) {
        case TradingState::OFF:    return "OFF";
        case TradingState::SHADOW: return "SHADOW";
        case TradingState::WARN:   return "WARN";
        case TradingState::LIVE:   return "LIVE";
        case TradingState::PAUSE:  return "PAUSE";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SNAPSHOT EVENT TYPES
// ═══════════════════════════════════════════════════════════════════════════════
enum class SnapshotTrigger : uint8_t {
    STATE_CHANGE,
    EXPECTANCY_FLIP,
    SESSION_BOUNDARY,
    DIVERGENCE_BREACH,
    SLOPE_DECAY,
    MANUAL
};

inline const char* trigger_name(SnapshotTrigger t) noexcept {
    switch (t) {
        case SnapshotTrigger::STATE_CHANGE:     return "STATE_CHG";
        case SnapshotTrigger::EXPECTANCY_FLIP:  return "EXP_FLIP";
        case SnapshotTrigger::SESSION_BOUNDARY: return "SESSION";
        case SnapshotTrigger::DIVERGENCE_BREACH: return "DIV_BREACH";
        case SnapshotTrigger::SLOPE_DECAY:      return "SLOPE_DECAY";
        case SnapshotTrigger::MANUAL:           return "MANUAL";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SNAPSHOT DATA STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════
struct ExpectancySnapshot {
    std::string timestamp;
    std::string symbol;
    TradingState state;
    TradingState prev_state;
    PauseReason pause_reason;
    SnapshotTrigger trigger;
    double expectancy_bps;
    double expectancy_slope;
    double slope_delta;
    double divergence_bps;
    int divergence_streak;
    std::string regime;
    std::string session;
    double latency_ms;
    int trade_count;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SNAPSHOT LOGGER
// ═══════════════════════════════════════════════════════════════════════════════
class ExpectancySnapshotLogger {
public:
    struct Config {
        std::string log_dir = "./logs";
        std::string file_prefix = "expectancy_audit";
        bool use_jsonl = false;  // false = CSV, true = JSONL
        bool console_echo = true;
    };
    
    explicit ExpectancySnapshotLogger(const Config& config = Config{})
        : config_(config)
    {
        open_log_file();
    }
    
    ~ExpectancySnapshotLogger() {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Log a snapshot
    // ─────────────────────────────────────────────────────────────────────────
    void log(const ExpectancySnapshot& snap) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!file_.is_open()) {
            open_log_file();
        }
        
        if (config_.use_jsonl) {
            write_jsonl(snap);
        } else {
            write_csv(snap);
        }
        
        file_.flush();
        
        if (config_.console_echo) {
            echo_console(snap);
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Convenience: Log state change
    // ─────────────────────────────────────────────────────────────────────────
    void log_state_change(
        const std::string& symbol,
        TradingState prev_state,
        TradingState new_state,
        PauseReason reason,
        double expectancy_bps,
        double slope,
        double slope_delta,
        double divergence_bps,
        int divergence_streak,
        const std::string& regime,
        const std::string& session,
        double latency_ms,
        int trade_count
    ) {
        ExpectancySnapshot snap;
        snap.timestamp = get_iso_timestamp();
        snap.symbol = symbol;
        snap.state = new_state;
        snap.prev_state = prev_state;
        snap.pause_reason = reason;
        snap.trigger = SnapshotTrigger::STATE_CHANGE;
        snap.expectancy_bps = expectancy_bps;
        snap.expectancy_slope = slope;
        snap.slope_delta = slope_delta;
        snap.divergence_bps = divergence_bps;
        snap.divergence_streak = divergence_streak;
        snap.regime = regime;
        snap.session = session;
        snap.latency_ms = latency_ms;
        snap.trade_count = trade_count;
        
        log(snap);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Convenience: Log expectancy flip
    // ─────────────────────────────────────────────────────────────────────────
    void log_expectancy_flip(
        const std::string& symbol,
        TradingState state,
        double expectancy_bps,
        double slope,
        const std::string& regime,
        const std::string& session
    ) {
        ExpectancySnapshot snap;
        snap.timestamp = get_iso_timestamp();
        snap.symbol = symbol;
        snap.state = state;
        snap.prev_state = state;
        snap.pause_reason = PauseReason::NONE;
        snap.trigger = SnapshotTrigger::EXPECTANCY_FLIP;
        snap.expectancy_bps = expectancy_bps;
        snap.expectancy_slope = slope;
        snap.slope_delta = 0;
        snap.divergence_bps = 0;
        snap.divergence_streak = 0;
        snap.regime = regime;
        snap.session = session;
        snap.latency_ms = 0;
        snap.trade_count = 0;
        
        log(snap);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Convenience: Log session boundary
    // ─────────────────────────────────────────────────────────────────────────
    void log_session_boundary(
        const std::string& symbol,
        TradingState state,
        const std::string& old_session,
        const std::string& new_session,
        double expectancy_bps
    ) {
        ExpectancySnapshot snap;
        snap.timestamp = get_iso_timestamp();
        snap.symbol = symbol;
        snap.state = state;
        snap.prev_state = state;
        snap.pause_reason = PauseReason::NONE;
        snap.trigger = SnapshotTrigger::SESSION_BOUNDARY;
        snap.expectancy_bps = expectancy_bps;
        snap.expectancy_slope = 0;
        snap.slope_delta = 0;
        snap.divergence_bps = 0;
        snap.divergence_streak = 0;
        snap.regime = "";
        snap.session = new_session;
        snap.latency_ms = 0;
        snap.trade_count = 0;
        
        log(snap);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Convenience: Log divergence breach
    // ─────────────────────────────────────────────────────────────────────────
    void log_divergence_breach(
        const std::string& symbol,
        TradingState state,
        double divergence_bps,
        int streak,
        double shadow_pnl,
        double live_pnl
    ) {
        ExpectancySnapshot snap;
        snap.timestamp = get_iso_timestamp();
        snap.symbol = symbol;
        snap.state = state;
        snap.prev_state = state;
        snap.pause_reason = PauseReason::DIV;
        snap.trigger = SnapshotTrigger::DIVERGENCE_BREACH;
        snap.expectancy_bps = 0;
        snap.expectancy_slope = 0;
        snap.slope_delta = 0;
        snap.divergence_bps = divergence_bps;
        snap.divergence_streak = streak;
        snap.regime = "";
        snap.session = "";
        snap.latency_ms = 0;
        snap.trade_count = 0;
        
        log(snap);
    }

private:
    Config config_;
    std::ofstream file_;
    std::mutex mutex_;
    std::string current_date_;
    
    void open_log_file() {
        std::string date = get_date_string();
        if (date == current_date_ && file_.is_open()) {
            return;  // Already open for today
        }
        
        if (file_.is_open()) {
            file_.close();
        }
        
        current_date_ = date;
        std::string ext = config_.use_jsonl ? ".jsonl" : ".csv";
        std::string path = config_.log_dir + "/" + config_.file_prefix + "_" + date + ext;
        
        // Check if file exists (for CSV header)
        bool exists = std::ifstream(path).good();
        
        file_.open(path, std::ios::app);
        
        if (!exists && !config_.use_jsonl) {
            // Write CSV header
            file_ << "timestamp,symbol,state,prev_state,pause_reason,trigger,"
                  << "expectancy_bps,slope,slope_delta,divergence_bps,divergence_streak,"
                  << "regime,session,latency_ms,trade_count\n";
        }
    }
    
    void write_csv(const ExpectancySnapshot& snap) {
        file_ << snap.timestamp << ","
              << snap.symbol << ","
              << state_name(snap.state) << ","
              << state_name(snap.prev_state) << ","
              << pause_reason_code(snap.pause_reason) << ","
              << trigger_name(snap.trigger) << ","
              << std::fixed << std::setprecision(4) << snap.expectancy_bps << ","
              << std::setprecision(6) << snap.expectancy_slope << ","
              << snap.slope_delta << ","
              << std::setprecision(4) << snap.divergence_bps << ","
              << snap.divergence_streak << ","
              << snap.regime << ","
              << snap.session << ","
              << std::setprecision(2) << snap.latency_ms << ","
              << snap.trade_count << "\n";
    }
    
    void write_jsonl(const ExpectancySnapshot& snap) {
        file_ << "{\"ts\":\"" << snap.timestamp << "\""
              << ",\"symbol\":\"" << snap.symbol << "\""
              << ",\"state\":\"" << state_name(snap.state) << "\""
              << ",\"prev_state\":\"" << state_name(snap.prev_state) << "\""
              << ",\"reason\":\"" << pause_reason_code(snap.pause_reason) << "\""
              << ",\"trigger\":\"" << trigger_name(snap.trigger) << "\""
              << ",\"expectancy\":" << std::fixed << std::setprecision(4) << snap.expectancy_bps
              << ",\"slope\":" << std::setprecision(6) << snap.expectancy_slope
              << ",\"slope_delta\":" << snap.slope_delta
              << ",\"divergence\":" << std::setprecision(4) << snap.divergence_bps
              << ",\"div_streak\":" << snap.divergence_streak
              << ",\"regime\":\"" << snap.regime << "\""
              << ",\"session\":\"" << snap.session << "\""
              << ",\"latency_ms\":" << std::setprecision(2) << snap.latency_ms
              << ",\"trades\":" << snap.trade_count
              << "}\n";
    }
    
    void echo_console(const ExpectancySnapshot& snap) {
        std::cout << "[AUDIT] " << snap.timestamp 
                  << " | " << snap.symbol
                  << " | " << state_name(snap.prev_state) << " → " << state_name(snap.state);
        
        if (snap.pause_reason != PauseReason::NONE) {
            std::cout << " [" << pause_reason_code(snap.pause_reason) << "]";
        }
        
        std::cout << " | " << trigger_name(snap.trigger)
                  << " | E=" << std::fixed << std::setprecision(2) << snap.expectancy_bps
                  << " S=" << std::setprecision(4) << snap.expectancy_slope
                  << "\n";
    }
    
    static std::string get_iso_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        return ss.str();
    }
    
    static std::string get_date_string() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time), "%Y%m%d");
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYMBOL STATE TRACKER
// Tracks previous state per symbol to detect changes
// ═══════════════════════════════════════════════════════════════════════════════
class SymbolStateTracker {
public:
    struct SymbolState {
        TradingState state = TradingState::OFF;
        PauseReason pause_reason = PauseReason::NONE;
        double last_expectancy = 0.0;
        std::string last_session;
        bool expectancy_was_positive = false;
    };
    
    explicit SymbolStateTracker(ExpectancySnapshotLogger& logger)
        : logger_(logger)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Update symbol state and log if changed
    // Returns true if state changed
    // ─────────────────────────────────────────────────────────────────────────
    bool update(
        const std::string& symbol,
        TradingState new_state,
        PauseReason reason,
        double expectancy_bps,
        double slope,
        double slope_delta,
        double divergence_bps,
        int divergence_streak,
        const std::string& regime,
        const std::string& session,
        double latency_ms,
        int trade_count
    ) {
        auto& prev = states_[symbol];
        bool logged = false;
        
        // Check for state change
        if (new_state != prev.state) {
            logger_.log_state_change(
                symbol, prev.state, new_state, reason,
                expectancy_bps, slope, slope_delta,
                divergence_bps, divergence_streak,
                regime, session, latency_ms, trade_count
            );
            prev.state = new_state;
            prev.pause_reason = reason;
            logged = true;
        }
        
        // Check for expectancy sign flip
        bool is_positive = expectancy_bps > 0;
        if (is_positive != prev.expectancy_was_positive && 
            std::abs(expectancy_bps) > 0.05) {  // Ignore tiny fluctuations
            logger_.log_expectancy_flip(symbol, new_state, expectancy_bps, slope, regime, session);
            prev.expectancy_was_positive = is_positive;
            logged = true;
        }
        
        // Check for session boundary
        if (!prev.last_session.empty() && session != prev.last_session) {
            logger_.log_session_boundary(symbol, new_state, prev.last_session, session, expectancy_bps);
            logged = true;
        }
        prev.last_session = session;
        
        // Check for divergence breach (streak >= 10)
        if (divergence_streak >= 10 && std::abs(divergence_bps) > 1.0) {
            // v3.11 FIX: Moved from static to member (was shared across instances!)
            if (last_logged_streak_[symbol] < 10) {
                logger_.log_divergence_breach(symbol, new_state, divergence_bps, divergence_streak, 0, 0);
                logged = true;
            }
            last_logged_streak_[symbol] = divergence_streak;
        }
        
        prev.last_expectancy = expectancy_bps;
        return logged;
    }
    
    // Get current state for a symbol
    [[nodiscard]] const SymbolState* get(const std::string& symbol) const {
        auto it = states_.find(symbol);
        if (it == states_.end()) return nullptr;
        return &it->second;
    }
    
    // Get pause reason code for GUI
    [[nodiscard]] const char* get_pause_reason_code(const std::string& symbol) const {
        auto it = states_.find(symbol);
        if (it == states_.end()) return "";
        return pause_reason_code(it->second.pause_reason);
    }

private:
    ExpectancySnapshotLogger& logger_;
    std::unordered_map<std::string, SymbolState> states_;
    std::unordered_map<std::string, int> last_logged_streak_;  // v3.11: Was static (shared across instances!)
};

} // namespace Shadow
} // namespace Chimera
