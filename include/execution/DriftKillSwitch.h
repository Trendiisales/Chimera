#pragma once
#include "config/DriftParameters.h"
#include <deque>
#include <cstdint>

class DriftKillSwitch {
public:
    DriftKillSwitch() 
        : enabled_(true)
        , drift_trades_()
        , spread_violation_start_(0)
    {}
    
    bool is_enabled() const { return enabled_; }
    
    void disable(const char* reason) {
        enabled_ = false;
        std::cout << "[DRIFT] KILL-SWITCH TRIGGERED: " << reason << "\n";
    }
    
    void enable() {
        enabled_ = true;
        drift_trades_.clear();
    }
    
    // Record drift trade result
    void record_trade(double pnl) {
        drift_trades_.push_back(pnl);
        if (drift_trades_.size() > 20) {
            drift_trades_.pop_front();
        }
        
        check_conditions();
    }
    
    // Check latency condition
    void check_latency(double p95_ms) {
        if (p95_ms > DriftConfig::KillSwitch::LATENCY_P95_MAX) {
            disable("LATENCY_DEGRADED");
        }
    }
    
    // Check spread condition
    void check_spread(double spread, double max_spread, uint64_t now_ms) {
        if (spread > max_spread) {
            if (spread_violation_start_ == 0) {
                spread_violation_start_ = now_ms;
            } else if (now_ms - spread_violation_start_ > DriftConfig::KillSwitch::SPREAD_VIOLATION_MS) {
                disable("SPREAD_VIOLATION");
            }
        } else {
            spread_violation_start_ = 0;
        }
    }

private:
    void check_conditions() {
        if (drift_trades_.size() < 20) return;
        
        // Check PnL of last 20
        double total_pnl = 0.0;
        for (double pnl : drift_trades_) {
            total_pnl += pnl;
        }
        
        if (total_pnl < DriftConfig::KillSwitch::PNL_LAST_20_MIN) {
            disable("PNL_LOSS_THRESHOLD");
            return;
        }
        
        // Check win rate
        int wins = 0;
        for (double pnl : drift_trades_) {
            if (pnl > 0) wins++;
        }
        double win_rate = static_cast<double>(wins) / drift_trades_.size();
        
        if (win_rate < DriftConfig::KillSwitch::WIN_RATE_MIN) {
            disable("WIN_RATE_THRESHOLD");
        }
    }

private:
    bool enabled_;
    std::deque<double> drift_trades_;
    uint64_t spread_violation_start_;
};
