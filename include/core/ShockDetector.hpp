#pragma once
// =============================================================================
// CHIMERA SHOCK DETECTOR - v4.7.0
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Chimera {

enum class SessionType : uint8_t {
    ASIA = 0,
    LONDON = 1,
    NY_OPEN = 2,
    NY_CONTINUATION = 3,
    OFF_HOURS = 4
};

inline const char* sessionTypeToString(SessionType s) {
    switch (s) {
        case SessionType::ASIA:            return "ASIA";
        case SessionType::LONDON:          return "LONDON";
        case SessionType::NY_OPEN:         return "NY_OPEN";
        case SessionType::NY_CONTINUATION: return "NY_CONTINUATION";
        case SessionType::OFF_HOURS:       return "OFF_HOURS";
        default:                           return "UNKNOWN";
    }
}

struct ShockMetrics {
    double range_1s = 0.0;
    double range_10s = 0.0;
    double volume_spike = 0.0;
    int spread_jumps = 0;
    uint64_t timestamp_ns = 0;
    
    void reset() {
        range_1s = 0.0;
        range_10s = 0.0;
        volume_spike = 0.0;
        spread_jumps = 0;
    }
};

class ShockDetector {
public:
    static ShockDetector& instance() {
        static ShockDetector inst;
        return inst;
    }
    
    void update(const char* symbol, double price, double spread, double volume, uint64_t now_ns) {
        updateRollingRange(price, now_ns);
        updateSpreadJumps(spread);
        updateVolumeSpike(volume);
        
        if (detectShock()) {
            if (!in_shock_.load(std::memory_order_relaxed)) {
                printf("[SHOCK] DETECTED on %s - range_1s=%.4f range_10s=%.4f vol_spike=%.1f spread_jumps=%d\n",
                       symbol, metrics_.range_1s, metrics_.range_10s, metrics_.volume_spike, metrics_.spread_jumps);
            }
            in_shock_.store(true, std::memory_order_release);
            shock_start_ns_ = now_ns;
        }
        
        uint64_t cooldown_ns = getCooldownNs();
        if (in_shock_.load(std::memory_order_relaxed) && now_ns > shock_start_ns_ + cooldown_ns) {
            printf("[SHOCK] CLEARED - cooldown complete\n");
            in_shock_.store(false, std::memory_order_release);
            metrics_.reset();
        }
    }
    
    bool isShock() const { return in_shock_.load(std::memory_order_acquire); }
    void setSession(SessionType s) { session_.store(s, std::memory_order_relaxed); }
    SessionType getSession() const { return session_.load(std::memory_order_relaxed); }
    ShockMetrics getMetrics() const { return metrics_; }
    
    void triggerShock() {
        in_shock_.store(true, std::memory_order_release);
        shock_start_ns_ = getCurrentNs();
        printf("[SHOCK] MANUALLY TRIGGERED\n");
    }
    
    void clearShock() {
        in_shock_.store(false, std::memory_order_release);
        metrics_.reset();
        printf("[SHOCK] MANUALLY CLEARED\n");
    }

private:
    ShockDetector() : in_shock_(false), session_(SessionType::OFF_HOURS), shock_start_ns_(0),
                      last_price_(0.0), baseline_spread_(0.0), baseline_volume_(0.0) {}
    
    bool detectShock() const {
        SessionType sess = session_.load(std::memory_order_relaxed);
        double range_mult = (sess == SessionType::NY_OPEN) ? 6.0 : 4.0;
        double vol_mult = (sess == SessionType::ASIA || sess == SessionType::OFF_HOURS) ? 3.5 : 6.0;
        
        if (metrics_.range_10s > 0.0 && metrics_.range_1s > range_mult * metrics_.range_10s) return true;
        if (metrics_.volume_spike > vol_mult) return true;
        if (metrics_.spread_jumps >= 3) return true;
        return false;
    }
    
    uint64_t getCooldownNs() const { return 120ULL * 1000000000ULL; }
    
    void updateRollingRange(double price, uint64_t now_ns) {
        if (last_price_ == 0.0) { last_price_ = price; return; }
        double change = std::fabs(price - last_price_) / last_price_;
        metrics_.range_1s = 0.3 * change + 0.7 * metrics_.range_1s;
        metrics_.range_10s = 0.05 * change + 0.95 * metrics_.range_10s;
        last_price_ = price;
        metrics_.timestamp_ns = now_ns;
    }
    
    void updateSpreadJumps(double spread) {
        if (baseline_spread_ == 0.0) { baseline_spread_ = spread; return; }
        if (spread > baseline_spread_ * 1.5) metrics_.spread_jumps++;
        else metrics_.spread_jumps = 0;
        baseline_spread_ = 0.99 * baseline_spread_ + 0.01 * spread;
    }
    
    void updateVolumeSpike(double volume) {
        if (baseline_volume_ == 0.0) { baseline_volume_ = volume; return; }
        if (baseline_volume_ > 0.0) metrics_.volume_spike = volume / baseline_volume_;
        baseline_volume_ = 0.99 * baseline_volume_ + 0.01 * volume;
    }
    
    static uint64_t getCurrentNs() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }
    
    std::atomic<bool> in_shock_;
    std::atomic<SessionType> session_;
    uint64_t shock_start_ns_;
    ShockMetrics metrics_;
    double last_price_;
    double baseline_spread_;
    double baseline_volume_;
};

inline ShockDetector& getShockDetector() { return ShockDetector::instance(); }
inline bool isMarketShock() { return ShockDetector::instance().isShock(); }

} // namespace Chimera
