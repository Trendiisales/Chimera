// =============================================================================
// BringUpSystem.hpp - Venue Bring-Up, Suppression Visibility, Auto-Promotion
// =============================================================================
// v6.69: Complete visibility system for understanding why trades don't fire
//
// Features:
// - Suppression taxonomy (RISK vs EXEC layer)
// - Venue health ladder (L0-L4)
// - Auto-promotion based on clean fills
// - CSV logging for post-mortem analysis
// - Dashboard JSON contract
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>
#include <atomic>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Chimera {

// =============================================================================
// Suppression Reason Taxonomy
// =============================================================================
enum class SuppressionLayer : uint8_t {
    NONE = 0,
    RISK = 1,      // Blocked by risk scaler (size = 0)
    EXEC = 2       // Blocked by execution gate (order vetoed)
};

enum class SuppressionReason : uint8_t {
    NONE = 0,
    
    // RISK layer suppressions (size collapsed to 0)
    LATENCY_ZERO = 1,           // Latency factor killed size
    VENUE_HEALTH_RED = 2,       // Venue health RED
    VENUE_HEALTH_YELLOW = 3,    // Venue health YELLOW (reduced size)
    DRAWDOWN_GUARD = 4,         // DD limiter active
    LIQUIDITY_ZERO = 5,         // Liquidity factor collapsed
    SESSION_WEIGHT_ZERO = 6,    // Session disabled (off-hours)
    VOLATILITY_BLOCK = 7,       // Vol regime blocked
    SPREAD_VETO = 8,            // Spread too wide
    MIN_R_NOT_MET = 9,          // Total R < min_R threshold
    
    // EXEC layer suppressions (order vetoed)
    FIX_NOT_LIVE = 20,          // FIX session not LIVE_TRADING
    FIX_LOGON_ONLY = 21,        // FIX logged on but not active
    BACKPRESSURE = 22,          // TX queue pressure
    COOLDOWN_ACTIVE = 23,       // Strategy cooldown
    SAFETY_ARMED = 24,          // Global safety switch
    RATE_LIMIT = 25,            // Venue rate limit
    MAX_POSITION = 26,          // Position limit reached
    MAX_ORDERS_FLIGHT = 27,     // Too many orders in flight
    STALE_TICK = 28,            // Tick data too old
    LOW_CONFIDENCE = 29,        // Confidence below threshold
    NO_CONSENSUS = 30,          // No strategy consensus
    INTENT_MISALIGNED = 31,     // Intent doesn't match regime
    
    // Grace/Bring-up
    LADDER_BLOCKED = 40,        // Ladder level = 0
    BRING_UP_CAP = 41           // Capped by bring-up profile
};

inline const char* toString(SuppressionReason r) {
    switch (r) {
        case SuppressionReason::NONE: return "NONE";
        case SuppressionReason::LATENCY_ZERO: return "LATENCY_ZERO";
        case SuppressionReason::VENUE_HEALTH_RED: return "VENUE_HEALTH_RED";
        case SuppressionReason::VENUE_HEALTH_YELLOW: return "VENUE_HEALTH_YELLOW";
        case SuppressionReason::DRAWDOWN_GUARD: return "DRAWDOWN_GUARD";
        case SuppressionReason::LIQUIDITY_ZERO: return "LIQUIDITY_ZERO";
        case SuppressionReason::SESSION_WEIGHT_ZERO: return "SESSION_WEIGHT_ZERO";
        case SuppressionReason::VOLATILITY_BLOCK: return "VOLATILITY_BLOCK";
        case SuppressionReason::SPREAD_VETO: return "SPREAD_VETO";
        case SuppressionReason::MIN_R_NOT_MET: return "MIN_R_NOT_MET";
        case SuppressionReason::FIX_NOT_LIVE: return "FIX_NOT_LIVE";
        case SuppressionReason::FIX_LOGON_ONLY: return "FIX_LOGON_ONLY";
        case SuppressionReason::BACKPRESSURE: return "BACKPRESSURE";
        case SuppressionReason::COOLDOWN_ACTIVE: return "COOLDOWN_ACTIVE";
        case SuppressionReason::SAFETY_ARMED: return "SAFETY_ARMED";
        case SuppressionReason::RATE_LIMIT: return "RATE_LIMIT";
        case SuppressionReason::MAX_POSITION: return "MAX_POSITION";
        case SuppressionReason::MAX_ORDERS_FLIGHT: return "MAX_ORDERS_FLIGHT";
        case SuppressionReason::STALE_TICK: return "STALE_TICK";
        case SuppressionReason::LOW_CONFIDENCE: return "LOW_CONFIDENCE";
        case SuppressionReason::NO_CONSENSUS: return "NO_CONSENSUS";
        case SuppressionReason::INTENT_MISALIGNED: return "INTENT_MISALIGNED";
        case SuppressionReason::LADDER_BLOCKED: return "LADDER_BLOCKED";
        case SuppressionReason::BRING_UP_CAP: return "BRING_UP_CAP";
        default: return "UNKNOWN";
    }
}

inline const char* toString(SuppressionLayer l) {
    switch (l) {
        case SuppressionLayer::NONE: return "NONE";
        case SuppressionLayer::RISK: return "RISK";
        case SuppressionLayer::EXEC: return "EXEC";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Suppression Event - One per blocked intent
// =============================================================================
struct SuppressionEvent {
    uint64_t timestamp_ns = 0;
    char symbol[16] = {0};
    char venue[16] = {0};           // "BINANCE" or "CFD"
    char strategy_id[32] = {0};
    int8_t intent_direction = 0;    // 1=LONG, -1=SHORT, 0=FLAT
    double base_size = 0.0;
    double final_size = 0.0;
    SuppressionLayer layer = SuppressionLayer::NONE;
    SuppressionReason reason = SuppressionReason::NONE;
    
    // Contextual data (filled as available)
    char venue_health[16] = {0};    // GREEN/YELLOW/RED
    uint8_t ladder_level = 0;
    double latency_ms = 0.0;
    double latency_cutoff_ms = 0.0;
    char fix_state[32] = {0};
    double spread_bps = 0.0;
    double drawdown_pct = 0.0;
    bool bring_up_enabled = false;
    
    // Helpers
    void setSymbol(const char* s) { std::strncpy(symbol, s, 15); symbol[15] = '\0'; }
    void setVenue(const char* v) { std::strncpy(venue, v, 15); venue[15] = '\0'; }
    void setStrategy(const char* s) { std::strncpy(strategy_id, s, 31); strategy_id[31] = '\0'; }
    void setVenueHealth(const char* h) { std::strncpy(venue_health, h, 15); venue_health[15] = '\0'; }
    void setFixState(const char* f) { std::strncpy(fix_state, f, 31); fix_state[31] = '\0'; }
};

// =============================================================================
// Venue Health with Ladder State
// =============================================================================
enum class VenueHealthState : uint8_t {
    RED = 0,      // Hard stop
    YELLOW = 1,   // Degraded but usable
    GREEN = 2     // Fully live
};

inline const char* toString(VenueHealthState h) {
    switch (h) {
        case VenueHealthState::RED: return "RED";
        case VenueHealthState::YELLOW: return "YELLOW";
        case VenueHealthState::GREEN: return "GREEN";
        default: return "UNKNOWN";
    }
}

struct VenueHealth {
    VenueHealthState health = VenueHealthState::YELLOW;
    bool bring_up_enabled = true;
    uint8_t ladder_level = 1;       // 0-4 (L0=blocked, L1=5%, L2=10%, L3=25%, L4=100%)
    uint16_t clean_fills = 0;
    uint64_t last_transition_ns = 0;
    uint64_t cooldown_until_ns = 0;
    SuppressionReason last_blocker = SuppressionReason::NONE;
    
    // Ladder scale lookup
    static constexpr double LADDER_SCALES[5] = {0.0, 0.05, 0.10, 0.25, 1.0};
    
    double getLadderScale() const {
        return (ladder_level < 5) ? LADDER_SCALES[ladder_level] : 1.0;
    }
    
    // Required clean fills per ladder level (to advance)
    uint16_t getRequiredFills() const {
        switch (ladder_level) {
            case 1: return 10;   // L1 → L2
            case 2: return 15;   // L2 → L3
            case 3: return 20;   // L3 → L4
            default: return 0;
        }
    }
};

// =============================================================================
// Bring-Up Profile Configuration
// =============================================================================
struct BringUpConfig {
    bool enabled = true;
    
    // Global caps
    double max_position_scale = 0.10;
    uint32_t max_orders_per_min = 3;
    
    // Health behavior
    double yellow_scale = 0.05;
    double green_scale = 1.0;
    double red_scale = 0.0;
    
    // Latency behavior
    double latency_soft_cutoff_ms = 120.0;
    double latency_hard_cutoff_ms = 180.0;
    double latency_soft_scale = 0.10;
    
    // Promotion rules
    bool auto_promotion_enabled = true;
    uint16_t promotion_required_fills = 25;
    double promotion_max_latency_ms = 130.0;
    double promotion_max_slippage_bps = 2.5;
    uint32_t promotion_window_minutes = 30;
    
    // Demotion rules
    bool demote_on_reject = true;
    bool demote_on_latency_breach = true;
    uint32_t demotion_cooldown_minutes = 10;
};

// Symbol class for different promotion thresholds
enum class SymbolClass : uint8_t {
    CRYPTO = 0,
    CFD = 1,
    FX = 2
};

inline SymbolClass getSymbolClass(const char* symbol) {
    if (strstr(symbol, "USDT") || strstr(symbol, "BTC") || strstr(symbol, "ETH") || strstr(symbol, "SOL")) {
        return SymbolClass::CRYPTO;
    }
    if (strstr(symbol, "XAU") || strstr(symbol, "XAG") || strstr(symbol, "NAS") || strstr(symbol, "SPX") || strstr(symbol, "US30")) {
        return SymbolClass::CFD;
    }
    return SymbolClass::FX;
}

// =============================================================================
// Suppression CSV Logger
// =============================================================================
class SuppressionLogger {
public:
    SuppressionLogger() = default;
    
    void setOutputDir(const std::string& dir) {
        output_dir_ = dir;
    }
    
    void log(const SuppressionEvent& evt) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Ensure file is open for today
        ensureFileOpen();
        
        if (!file_.is_open()) return;
        
        // Format timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif
        
        char time_str[64];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        
        // Write CSV row
        file_ << time_str << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z,"
              << evt.symbol << ","
              << evt.venue << ","
              << evt.strategy_id << ","
              << (int)evt.intent_direction << ","
              << evt.base_size << ","
              << evt.final_size << ","
              << toString(evt.layer) << ","
              << toString(evt.reason) << ","
              << evt.venue_health << ","
              << evt.ladder_level << ","
              << evt.latency_ms << ","
              << evt.latency_cutoff_ms << ","
              << evt.fix_state << ","
              << evt.spread_bps << ","
              << evt.drawdown_pct << ","
              << (evt.bring_up_enabled ? "true" : "false")
              << "\n";
        
        file_.flush();
        
        // Update counters
        suppression_counts_[evt.reason]++;
        total_suppressions_++;
        
        // Track last suppression per symbol
        last_suppression_[evt.symbol] = evt;
    }
    
    uint64_t getCount(SuppressionReason reason) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = suppression_counts_.find(reason);
        return (it != suppression_counts_.end()) ? it->second : 0;
    }
    
    uint64_t getTotalCount() const { return total_suppressions_.load(); }
    
    const SuppressionEvent* getLastSuppression(const char* symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_suppression_.find(symbol);
        return (it != last_suppression_.end()) ? &it->second : nullptr;
    }
    
    // Get suppression counts as JSON for dashboard
    std::string getCountsJSON() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& [reason, count] : suppression_counts_) {
            if (!first) ss << ",";
            ss << "\"" << toString(reason) << "\":" << count;
            first = false;
        }
        ss << "}";
        return ss.str();
    }
    
private:
    void ensureFileOpen() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif
        
        char date_str[16];
        std::strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_buf);
        
        if (current_date_ != date_str || !file_.is_open()) {
            if (file_.is_open()) file_.close();
            
            current_date_ = date_str;
            std::string filename = output_dir_ + "/suppressions_" + date_str + ".csv";
            
            // Check if file exists (for header)
            bool exists = std::ifstream(filename).good();
            
            file_.open(filename, std::ios::app);
            
            // Write header if new file
            if (!exists && file_.is_open()) {
                file_ << "timestamp,symbol,venue,strategy_id,intent_direction,"
                      << "base_size,final_size,suppression_layer,suppression_reason,"
                      << "venue_health,ladder_level,latency_ms,latency_cutoff_ms,"
                      << "fix_state,spread_bps,drawdown_pct,bring_up_enabled\n";
            }
        }
    }
    
    std::string output_dir_ = ".";
    std::string current_date_;
    std::ofstream file_;
    mutable std::mutex mutex_;
    std::unordered_map<SuppressionReason, uint64_t> suppression_counts_;
    std::unordered_map<std::string, SuppressionEvent> last_suppression_;
    std::atomic<uint64_t> total_suppressions_{0};
};

// =============================================================================
// Bring-Up Manager - Tracks venue health & ladder for all symbols
// =============================================================================
class BringUpManager {
public:
    BringUpManager() {
        logger_.setOutputDir("./logs");
    }
    
    void setConfig(const BringUpConfig& cfg) { config_ = cfg; }
    const BringUpConfig& getConfig() const { return config_; }
    
    // Get or create venue health for a symbol
    VenueHealth& getHealth(const char* symbol, const char* venue) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(symbol) + "_" + venue;
        if (health_map_.find(key) == health_map_.end()) {
            health_map_[key] = VenueHealth{};
            health_map_[key].bring_up_enabled = config_.enabled;
        }
        return health_map_[key];
    }
    
    // Record a clean fill (for promotion)
    void recordCleanFill(const char* symbol, const char* venue, double latency_ms, double slippage_bps) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(symbol) + "_" + venue;
        
        auto& h = health_map_[key];
        SymbolClass cls = getSymbolClass(symbol);
        
        // Check if fill qualifies as "clean"
        double max_lat = (cls == SymbolClass::CRYPTO) ? 150.0 : 80.0;
        double max_slip = (cls == SymbolClass::CRYPTO) ? 3.0 : 1.5;
        
        if (latency_ms <= max_lat && slippage_bps <= max_slip) {
            h.clean_fills++;
            
            // Check for promotion
            if (config_.auto_promotion_enabled && h.health == VenueHealthState::YELLOW) {
                uint16_t required = h.getRequiredFills();
                if (h.clean_fills >= required && h.ladder_level < 4) {
                    h.ladder_level++;
                    h.clean_fills = 0;
                    h.last_transition_ns = nowNs();
                    
                    // Promote to GREEN when reaching L4
                    if (h.ladder_level == 4) {
                        h.health = VenueHealthState::GREEN;
                    }
                    
                    std::cout << "[BringUp] " << symbol << " promoted to L" 
                              << (int)h.ladder_level << " (" << toString(h.health) << ")\n";
                }
            }
        }
    }
    
    // Record a fault (for demotion)
    void recordFault(const char* symbol, const char* venue, SuppressionReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(symbol) + "_" + venue;
        
        auto& h = health_map_[key];
        h.last_blocker = reason;
        
        // Demote on reject or latency breach
        bool should_demote = (config_.demote_on_reject && 
            (reason == SuppressionReason::FIX_NOT_LIVE ||
             reason == SuppressionReason::BACKPRESSURE ||
             reason == SuppressionReason::RATE_LIMIT)) ||
            (config_.demote_on_latency_breach && 
             reason == SuppressionReason::LATENCY_ZERO);
        
        if (should_demote && h.ladder_level > 0) {
            h.ladder_level--;
            h.clean_fills = 0;
            h.health = (h.ladder_level == 0) ? VenueHealthState::RED : VenueHealthState::YELLOW;
            h.cooldown_until_ns = nowNs() + config_.demotion_cooldown_minutes * 60ULL * 1000000000ULL;
            h.last_transition_ns = nowNs();
            
            std::cout << "[BringUp] " << symbol << " DEMOTED to L" 
                      << (int)h.ladder_level << " (" << toString(h.health) << ")"
                      << " reason=" << toString(reason) << "\n";
        }
    }
    
    // Log a suppression event
    void logSuppression(const SuppressionEvent& evt) {
        logger_.log(evt);
        
        // Also record as fault if it's a significant suppression
        if (evt.reason != SuppressionReason::NONE &&
            evt.reason != SuppressionReason::VENUE_HEALTH_YELLOW) {
            // Don't auto-demote for every suppression, just track
        }
    }
    
    // Get effective size multiplier for a symbol
    double getEffectiveSizeMultiplier(const char* symbol, const char* venue) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(symbol) + "_" + venue;
        
        auto it = health_map_.find(key);
        if (it == health_map_.end()) return 1.0;
        
        const auto& h = it->second;
        
        if (!h.bring_up_enabled) return 1.0;
        
        switch (h.health) {
            case VenueHealthState::RED: return config_.red_scale;
            case VenueHealthState::YELLOW: return h.getLadderScale();
            case VenueHealthState::GREEN: return config_.green_scale;
            default: return 1.0;
        }
    }
    
    // Get dashboard JSON for all venues
    std::string getDashboardJSON() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        
        ss << "{\"venues\":[";
        bool first = true;
        for (const auto& [key, h] : health_map_) {
            if (!first) ss << ",";
            
            // Parse symbol and venue from key
            size_t pos = key.find('_');
            std::string symbol = key.substr(0, pos);
            std::string venue = key.substr(pos + 1);
            
            ss << "{\"symbol\":\"" << symbol << "\","
               << "\"venue\":\"" << venue << "\","
               << "\"health\":\"" << toString(h.health) << "\","
               << "\"ladder_level\":" << (int)h.ladder_level << ","
               << "\"ladder_scale\":" << h.getLadderScale() << ","
               << "\"clean_fills\":" << h.clean_fills << ","
               << "\"required_fills\":" << h.getRequiredFills() << ","
               << "\"last_blocker\":\"" << toString(h.last_blocker) << "\","
               << "\"bring_up_enabled\":" << (h.bring_up_enabled ? "true" : "false") << "}";
            first = false;
        }
        ss << "],";
        
        // Suppression counters
        ss << "\"suppression_counts\":" << logger_.getCountsJSON() << ",";
        ss << "\"total_suppressions\":" << logger_.getTotalCount();
        
        ss << "}";
        return ss.str();
    }
    
    SuppressionLogger& getLogger() { return logger_; }
    
private:
    static uint64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    BringUpConfig config_;
    std::unordered_map<std::string, VenueHealth> health_map_;
    SuppressionLogger logger_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Global instance
// =============================================================================
inline BringUpManager& getBringUpManager() {
    static BringUpManager instance;
    return instance;
}

} // namespace Chimera
