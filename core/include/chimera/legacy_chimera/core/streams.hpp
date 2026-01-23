#ifndef STREAMS_HPP
#define STREAMS_HPP

#include "chimera/core/system_state.hpp"
#include "chimera/signal_bridge.hpp"
#include "chimera/lag_model.hpp"
#include <string>
#include <mutex>
#include <atomic>

class FollowerStream {
public:
    FollowerStream(
        const std::string& symbol,
        LagModel& lag,
        SignalBridge& bridge
    ) : symbol_(symbol), lag_(lag), bridge_(bridge) {}

    void onCascade(const CascadeEvent& ev) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        pending_side_ = ev.side;
        cascade_ts_ = ev.ts_ns;
        cascade_strength_ = ev.strength;
        has_pending_ = true;
    }

    void onTick(uint64_t ts_ns, double price) {
        lag_.recordFollower(symbol_, ts_ns, price);
        last_price_ = price;
    }

    bool shouldTrade(uint64_t now_ns) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (!has_pending_)
            return false;
        
        if (bridge_.followersBlocked(now_ns))
            return false;
        
        auto stats = lag_.getStats(symbol_);
        if (!stats.tradeable)
            return false;
        
        uint64_t age_ns = now_ns - cascade_ts_;
        double age_ms = age_ns / 1e6;
        
        double target_lag = stats.mean_lag_ms * 0.8;
        
        if (age_ms < target_lag)
            return false;
        
        if (age_ms > max_age_ms_) {
            has_pending_ = false;
            return false;
        }
        
        return true;
    }

    Side side() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return pending_side_;
    }

    void markExecuted() {
        std::lock_guard<std::mutex> lock(mtx_);
        has_pending_ = false;
        
        bridge_.blockBTC(cascade_ts_ + exhaustion_block_ns_);
    }

    std::string symbol() const { return symbol_; }

    double cascadeStrength() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return cascade_strength_;
    }

    void setMaxAge(double ms) { max_age_ms_ = ms; }
    void setExhaustionBlock(uint64_t ns) { exhaustion_block_ns_ = ns; }

private:
    std::string symbol_;
    LagModel& lag_;
    SignalBridge& bridge_;
    
    mutable std::mutex mtx_;
    
    Side pending_side_ = Side::NONE;
    uint64_t cascade_ts_ = 0;
    double cascade_strength_ = 0.0;
    bool has_pending_ = false;
    
    std::atomic<double> last_price_{0.0};
    
    double max_age_ms_ = 500.0;
    uint64_t exhaustion_block_ns_ = 1'000'000'000ULL;
};

#endif
