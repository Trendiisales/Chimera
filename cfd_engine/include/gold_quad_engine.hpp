/**
 * Gold Quad Engine v5.9.1 FINAL
 * ==============================
 * Production-locked XAUUSD microstructure trading engine
 * 
 * ENGINE ARCHITECTURE:
 * - MR:  Mean Revert (liquidity harvesting, high freq)
 * - SF:  Stop Fade (stop inefficiency, medium freq)
 * - SRM: Sweep Repricing Momentum (micro momentum, low freq)
 * - GRI: Gold Regime Ignition (macro momentum, very low freq)
 * 
 * Integration: Include this header in Chimera CFD engine
 * 
 * LOCKED - DO NOT MODIFY PARAMETERS WITHOUT FULL REVALIDATION
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <deque>
#include <array>

namespace gold {

// =============================================================================
// CONFIGURATION (LOCKED)
// =============================================================================

struct GoldConfig {
    // Point value
    static constexpr double POINT_VALUE = 100.0;
    
    // Engine weights
    static constexpr double WEIGHT_MR = 0.75;
    static constexpr double WEIGHT_SF = 1.25;
    static constexpr double WEIGHT_SRM = 2.25;
    static constexpr double WEIGHT_GRI = 3.00;
    
    // Daily caps
    static constexpr int SF_DAILY_CAP = 3;
    static constexpr int SRM_DAILY_CAP = 2;
    static constexpr int GRI_DAILY_CAP = 1;
    
    // Risk limits
    static constexpr double DAILY_LOSS_CAP = 2500.0;
    static constexpr double SRM_DAILY_LOSS_CAP = 1200.0;
    
    // MR parameters
    static constexpr double MR_TP = 0.30;
    static constexpr double MR_SL = 0.18;
    static constexpr double MR_VEL_THRESH = 0.35;
    static constexpr int64_t MR_COOLDOWN_MS = 3000;
    static constexpr int64_t MR_VEL_WINDOW_MS = 2000;
    static constexpr int64_t MR_STALL_WINDOW_MS = 500;
    static constexpr double MR_STALL_EPS = 0.08;
    
    // SF parameters
    static constexpr double SF_MIN_SWEEP = 0.50;
    static constexpr double SF_TP = 0.70;
    static constexpr double SF_SL = 0.35;
    static constexpr int64_t SF_STALL_WINDOW_MS = 400;
    static constexpr double SF_STALL_EPS = 0.10;
    
    // SRM parameters (LOCKED)
    static constexpr double SRM_MIN_SWEEP = 0.60;
    static constexpr int64_t SRM_HOLD_WINDOW_MS = 400;
    static constexpr double SRM_HOLD_MAX_RANGE = 0.25;
    static constexpr double SRM_PRE_RANGE_MAX = 0.35;
    static constexpr double SRM_TP = 2.00;
    static constexpr double SRM_SL = 0.70;
    static constexpr double SRM_SIZE_BASE = 0.60;
    static constexpr double SRM_SIZE_MAX_MULT = 1.80;
    
    // GRI parameters (LOCKED)
    static constexpr double GRI_MIN_SWEEP = 1.50;
    static constexpr int64_t GRI_SWEEP_WINDOW_MS = 600;
    static constexpr double GRI_PRE_RANGE_MIN = 0.80;
    static constexpr double GRI_SL = 1.20;
    static constexpr double GRI_TP_PARTIAL = 2.00;
    static constexpr double GRI_RUNNER_TRAIL = 0.80;
    static constexpr double GRI_VEL_PCTL = 95.0;
    
    // State classification
    static constexpr double STATE_DEAD_RANGE = 0.30;
    static constexpr double STATE_DEAD_SPREAD = 0.80;
    static constexpr double STATE_EXP_RANGE = 5.0;
    static constexpr double STATE_SWEEP_SPREAD = 0.70;
};

// =============================================================================
// MARKET STATE
// =============================================================================

enum class MarketState : uint8_t {
    DEAD = 0,
    EXPANSION = 1,
    INV_CORR = 3,
    STOP_SWEEP = 4
};

// =============================================================================
// ENGINE IDS
// =============================================================================

enum class EngineId : uint8_t {
    MR = 0,
    SF = 1,
    SRM = 2,
    GRI = 3
};

// =============================================================================
// TRADE SIGNAL
// =============================================================================

struct TradeSignal {
    bool active = false;
    EngineId engine = EngineId::MR;
    int direction = 0;  // 1 = long, -1 = short
    double entry_price = 0.0;
    double tp_price = 0.0;
    double sl_price = 0.0;
    double size_mult = 1.0;
    int64_t entry_ts = 0;
};

// =============================================================================
// TICK BUFFER
// =============================================================================

struct TickData {
    int64_t ts;
    double mid;
    double spread;
    int hour;
};

class TickBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 500;
    
    void push(int64_t ts, double bid, double ask) {
        TickData tick;
        tick.ts = ts;
        tick.mid = (bid + ask) / 2.0;
        tick.spread = ask - bid;
        tick.hour = static_cast<int>((ts / 3600000) % 24);
        
        if (buffer_.size() >= BUFFER_SIZE) {
            buffer_.pop_front();
        }
        buffer_.push_back(tick);
    }
    
    size_t size() const { return buffer_.size(); }
    bool ready() const { return buffer_.size() >= 200; }
    
    const TickData& current() const { return buffer_.back(); }
    const TickData& at(size_t idx) const { return buffer_[idx]; }
    
    double calcVelocity(int64_t window_ms) const {
        if (buffer_.size() < 2) return 0.0;
        
        const auto& curr = buffer_.back();
        int64_t cutoff = curr.ts - window_ms;
        
        size_t j = buffer_.size() - 1;
        while (j > 0 && buffer_[j].ts >= cutoff) {
            --j;
        }
        
        if (j == buffer_.size() - 1) return 0.0;
        
        double dt = (curr.ts - buffer_[j].ts) / 1000.0;
        if (dt <= 0) return 0.0;
        
        return (curr.mid - buffer_[j].mid) / dt;
    }
    
    double calcRange(int64_t window_ms) const {
        if (buffer_.size() < 2) return 0.0;
        
        const auto& curr = buffer_.back();
        int64_t cutoff = curr.ts - window_ms;
        
        double hi = curr.mid;
        double lo = curr.mid;
        
        for (auto it = buffer_.rbegin(); it != buffer_.rend(); ++it) {
            if (it->ts < cutoff) break;
            hi = std::max(hi, it->mid);
            lo = std::min(lo, it->mid);
        }
        
        return hi - lo;
    }
    
    bool checkStall(int64_t window_ms, double eps) const {
        if (buffer_.size() < 5) return false;
        
        const auto& curr = buffer_.back();
        int64_t cutoff = curr.ts - window_ms;
        
        double hi = curr.mid;
        double lo = curr.mid;
        int count = 0;
        
        for (auto it = buffer_.rbegin(); it != buffer_.rend(); ++it) {
            if (it->ts < cutoff) break;
            hi = std::max(hi, it->mid);
            lo = std::min(lo, it->mid);
            ++count;
        }
        
        return (count >= 3) && ((hi - lo) < eps);
    }
    
    MarketState classifyState() const {
        if (buffer_.size() < 200) return MarketState::DEAD;
        
        size_t start = buffer_.size() - 200;
        double hi = buffer_[start].mid;
        double lo = buffer_[start].mid;
        double spread_sum = 0.0;
        
        for (size_t i = start; i < buffer_.size(); ++i) {
            hi = std::max(hi, buffer_[i].mid);
            lo = std::min(lo, buffer_[i].mid);
            spread_sum += buffer_[i].spread;
        }
        
        double rng = hi - lo;
        double spread_avg = spread_sum / 200.0;
        
        if (rng < GoldConfig::STATE_DEAD_RANGE && spread_avg > GoldConfig::STATE_DEAD_SPREAD) {
            return MarketState::DEAD;
        }
        if (rng > GoldConfig::STATE_EXP_RANGE) {
            return MarketState::EXPANSION;
        }
        if (spread_avg > GoldConfig::STATE_SWEEP_SPREAD) {
            return MarketState::STOP_SWEEP;
        }
        return MarketState::INV_CORR;
    }
    
private:
    std::deque<TickData> buffer_;
};

// =============================================================================
// GOLD QUAD ENGINE
// =============================================================================

class GoldQuadEngine {
public:
    GoldQuadEngine() = default;
    
    void setVelocityThreshold(double thresh) {
        gri_vel_threshold_ = thresh;
    }
    
    void onTick(int64_t ts, double bid, double ask) {
        buffer_.push(ts, bid, ask);
        
        if (!buffer_.ready()) return;
        
        const auto& tick = buffer_.current();
        double mid = tick.mid;
        int hour = tick.hour;
        
        // Daily reset
        int64_t day = ts / 86400000;
        if (current_day_ == 0) {
            current_day_ = day;
        } else if (day != current_day_) {
            resetDaily();
            current_day_ = day;
        }
        
        // Daily loss cap
        if (daily_pnl_ <= -GoldConfig::DAILY_LOSS_CAP) return;
        
        // SRM daily loss cap
        if (srm_daily_pnl_ <= -GoldConfig::SRM_DAILY_LOSS_CAP) {
            srm_disabled_today_ = true;
        }
        
        // SRM timeout
        if (srm_sweep_detected_ && ts - srm_sweep_ts_ > 2000) {
            resetSrmSweep();
        }
        
        MarketState state = buffer_.classifyState();
        
        // Process active trades
        processMR(ts, mid);
        processSF(ts, mid);
        processSRM(ts, mid);
        processGRI(ts, mid, hour);
        
        // Entry logic
        if (!mr_on_) checkMREntry(ts, mid, state);
        if (!sf_on_) checkSFEntry(ts, mid, state, hour);
        if (!srm_on_) checkSRMEntry(ts, mid, state, hour);
        if (!gri_on_) checkGRIEntry(ts, mid, hour);
    }
    
    // Getters for current positions
    bool hasMRPosition() const { return mr_on_; }
    bool hasSFPosition() const { return sf_on_; }
    bool hasSRMPosition() const { return srm_on_; }
    bool hasGRIPosition() const { return gri_on_; }
    
    TradeSignal getMRSignal() const {
        TradeSignal sig;
        sig.active = mr_on_;
        sig.engine = EngineId::MR;
        sig.direction = mr_dir_;
        sig.entry_price = mr_entry_;
        sig.tp_price = mr_entry_ + mr_dir_ * GoldConfig::MR_TP;
        sig.sl_price = mr_entry_ - mr_dir_ * GoldConfig::MR_SL;
        return sig;
    }
    
    TradeSignal getSFSignal() const {
        TradeSignal sig;
        sig.active = sf_on_;
        sig.engine = EngineId::SF;
        sig.direction = sf_dir_;
        sig.entry_price = sf_entry_;
        sig.tp_price = sf_entry_ + sf_dir_ * GoldConfig::SF_TP;
        sig.sl_price = sf_entry_ - sf_dir_ * GoldConfig::SF_SL;
        return sig;
    }
    
    TradeSignal getSRMSignal() const {
        TradeSignal sig;
        sig.active = srm_on_;
        sig.engine = EngineId::SRM;
        sig.direction = srm_dir_;
        sig.entry_price = srm_entry_;
        sig.tp_price = srm_entry_ + srm_dir_ * GoldConfig::SRM_TP;
        sig.sl_price = srm_entry_ - srm_dir_ * GoldConfig::SRM_SL;
        sig.size_mult = srm_size_mult_;
        return sig;
    }
    
    TradeSignal getGRISignal() const {
        TradeSignal sig;
        sig.active = gri_on_;
        sig.engine = EngineId::GRI;
        sig.direction = gri_dir_;
        sig.entry_price = gri_entry_;
        sig.sl_price = gri_entry_ - gri_dir_ * GoldConfig::GRI_SL;
        return sig;
    }
    
    double getDailyPnL() const { return daily_pnl_; }
    
private:
    void resetDaily() {
        daily_pnl_ = 0.0;
        sf_today_ = 0;
        srm_today_ = 0;
        srm_daily_pnl_ = 0.0;
        srm_disabled_today_ = false;
        gri_today_ = 0;
    }
    
    void resetSrmSweep() {
        srm_sweep_detected_ = false;
        srm_sweep_dir_ = 0;
        srm_sweep_ts_ = 0;
        srm_sweep_level_ = 0.0;
        srm_sweep_dist_ = 0.0;
    }
    
    // MR processing
    void processMR(int64_t ts, double mid) {
        if (!mr_on_) return;
        
        double pnl_pts = (mid - mr_entry_) * mr_dir_;
        
        if (pnl_pts >= GoldConfig::MR_TP || pnl_pts <= -GoldConfig::MR_SL) {
            double pnl = pnl_pts * GoldConfig::POINT_VALUE * GoldConfig::WEIGHT_MR;
            daily_pnl_ += pnl;
            mr_on_ = false;
            mr_cooldown_ = ts + GoldConfig::MR_COOLDOWN_MS;
        }
    }
    
    void checkMREntry(int64_t ts, double mid, MarketState state) {
        if (state != MarketState::INV_CORR) return;
        if (ts < mr_cooldown_) return;
        
        double vel = buffer_.calcVelocity(GoldConfig::MR_VEL_WINDOW_MS);
        double abs_vel = std::abs(vel);
        
        if (abs_vel > mr_peak_vel_) {
            mr_peak_vel_ = abs_vel;
            mr_peak_dir_ = (vel > 0) ? 1 : -1;
            mr_peak_ts_ = ts;
        }
        
        if (ts - mr_peak_ts_ > 1000) {
            mr_peak_vel_ *= 0.95;
        }
        
        if (mr_peak_vel_ >= GoldConfig::MR_VEL_THRESH) {
            if (buffer_.checkStall(GoldConfig::MR_STALL_WINDOW_MS, GoldConfig::MR_STALL_EPS)) {
                mr_on_ = true;
                mr_dir_ = -mr_peak_dir_;
                mr_entry_ = mid;
                mr_peak_vel_ = 0.0;
            }
        }
    }
    
    // SF processing
    void processSF(int64_t ts, double mid) {
        if (!sf_on_) return;
        
        double pnl_pts = (mid - sf_entry_) * sf_dir_;
        
        if (pnl_pts >= GoldConfig::SF_TP || pnl_pts <= -GoldConfig::SF_SL) {
            double pnl = pnl_pts * GoldConfig::POINT_VALUE * GoldConfig::WEIGHT_SF;
            daily_pnl_ += pnl;
            sf_on_ = false;
        }
    }
    
    void checkSFEntry(int64_t ts, double mid, MarketState state, int hour) {
        if (state != MarketState::STOP_SWEEP) {
            sf_sweep_ts_ = 0;
            sf_sweep_px_ = 0.0;
            return;
        }
        
        if (sf_today_ >= GoldConfig::SF_DAILY_CAP) return;
        
        if (sf_sweep_ts_ == 0) {
            sf_sweep_ts_ = ts;
            sf_sweep_px_ = mid;
        } else {
            int64_t dur = ts - sf_sweep_ts_;
            if (dur > 2000) {
                sf_sweep_ts_ = ts;
                sf_sweep_px_ = mid;
            } else {
                double move = mid - sf_sweep_px_;
                if (std::abs(move) >= GoldConfig::SF_MIN_SWEEP) {
                    if (buffer_.checkStall(GoldConfig::SF_STALL_WINDOW_MS, GoldConfig::SF_STALL_EPS)) {
                        sf_on_ = true;
                        sf_today_++;
                        sf_dir_ = (move > 0) ? -1 : 1;
                        sf_entry_ = mid;
                        sf_sweep_ts_ = 0;
                        sf_sweep_px_ = 0.0;
                    }
                }
            }
        }
    }
    
    // SRM processing
    void processSRM(int64_t ts, double mid) {
        if (!srm_on_) return;
        
        double pnl_pts = (mid - srm_entry_) * srm_dir_;
        
        if (pnl_pts >= GoldConfig::SRM_TP || pnl_pts <= -GoldConfig::SRM_SL) {
            double pnl = pnl_pts * GoldConfig::POINT_VALUE * GoldConfig::WEIGHT_SRM * srm_size_mult_;
            daily_pnl_ += pnl;
            srm_daily_pnl_ += pnl;
            srm_on_ = false;
            resetSrmSweep();
        }
    }
    
    void checkSRMEntry(int64_t ts, double mid, MarketState state, int hour) {
        if (state != MarketState::STOP_SWEEP) return;
        if (sf_on_) return;
        if (srm_today_ >= GoldConfig::SRM_DAILY_CAP) return;
        if (srm_disabled_today_) return;
        
        // Session block
        if (hour == 4 || hour == 5 || hour == 10 || hour == 21 || hour == 22) {
            resetSrmSweep();
            return;
        }
        
        // Pre-range check
        double pre_range = buffer_.calcRange(1500);
        if (pre_range > GoldConfig::SRM_PRE_RANGE_MAX) {
            resetSrmSweep();
            return;
        }
        
        // Sweep detection
        if (srm_sweep_level_ == 0.0) {
            srm_sweep_level_ = mid;
            srm_sweep_ts_ = ts;
            return;
        }
        
        double move = mid - srm_sweep_level_;
        
        if (std::abs(move) >= GoldConfig::SRM_MIN_SWEEP && !srm_sweep_detected_) {
            srm_sweep_detected_ = true;
            srm_sweep_dir_ = (move > 0) ? 1 : -1;
            srm_sweep_dist_ = std::abs(move);
            srm_sweep_ts_ = ts;
        } else if (srm_sweep_detected_) {
            if (std::abs(move) > srm_sweep_dist_) {
                srm_sweep_dist_ = std::abs(move);
            }
        }
        
        // Hold check
        if (srm_sweep_detected_ && !srm_on_) {
            int64_t time_since = ts - srm_sweep_ts_;
            
            if (time_since >= GoldConfig::SRM_HOLD_WINDOW_MS) {
                double hold_range = buffer_.calcRange(GoldConfig::SRM_HOLD_WINDOW_MS);
                
                if (hold_range < GoldConfig::SRM_HOLD_MAX_RANGE) {
                    srm_on_ = true;
                    srm_today_++;
                    srm_dir_ = srm_sweep_dir_;
                    srm_entry_ = mid;
                    
                    srm_size_mult_ = srm_sweep_dist_ / GoldConfig::SRM_SIZE_BASE;
                    srm_size_mult_ = std::clamp(srm_size_mult_, 1.0, GoldConfig::SRM_SIZE_MAX_MULT);
                    
                    resetSrmSweep();
                } else {
                    resetSrmSweep();
                }
            }
        }
    }
    
    // GRI processing
    void processGRI(int64_t ts, double mid, int hour) {
        if (!gri_on_) return;
        
        double pnl_pts = (mid - gri_entry_) * gri_dir_;
        
        // Update peak
        if (gri_dir_ == 1) {
            if (mid > gri_peak_) gri_peak_ = mid;
        } else {
            if (mid < gri_peak_) gri_peak_ = mid;
        }
        
        bool exit_trade = false;
        
        // Stop loss
        if (pnl_pts <= -GoldConfig::GRI_SL) {
            exit_trade = true;
        }
        
        // Partial TP
        if (!gri_partial_taken_ && pnl_pts >= GoldConfig::GRI_TP_PARTIAL) {
            gri_partial_taken_ = true;
        }
        
        // Runner trailing
        if (gri_partial_taken_) {
            double trail_stop;
            if (gri_dir_ == 1) {
                trail_stop = gri_peak_ - GoldConfig::GRI_RUNNER_TRAIL;
                if (mid <= trail_stop) exit_trade = true;
            } else {
                trail_stop = gri_peak_ + GoldConfig::GRI_RUNNER_TRAIL;
                if (mid >= trail_stop) exit_trade = true;
            }
        }
        
        // Session end
        if (!isGRISession(hour)) {
            exit_trade = true;
        }
        
        if (exit_trade) {
            double final_pnl_pts = (mid - gri_entry_) * gri_dir_;
            double pnl;
            if (gri_partial_taken_) {
                pnl = (GoldConfig::GRI_TP_PARTIAL * 0.5 + final_pnl_pts * 0.5) * 
                      GoldConfig::POINT_VALUE * GoldConfig::WEIGHT_GRI;
            } else {
                pnl = final_pnl_pts * GoldConfig::POINT_VALUE * GoldConfig::WEIGHT_GRI;
            }
            daily_pnl_ += pnl;
            gri_on_ = false;
            gri_partial_taken_ = false;
        }
    }
    
    void checkGRIEntry(int64_t ts, double mid, int hour) {
        if (gri_today_ >= GoldConfig::GRI_DAILY_CAP) return;
        if (!isGRISession(hour)) return;
        if (gri_on_) return;
        
        double pre_range = buffer_.calcRange(1000);
        if (pre_range < GoldConfig::GRI_PRE_RANGE_MIN) return;
        
        // Look for macro sweep
        const auto& curr = buffer_.current();
        
        for (size_t j = std::max(size_t(0), buffer_.size() - 100); j < buffer_.size(); ++j) {
            const auto& tick = buffer_.at(j);
            if (curr.ts - tick.ts > GoldConfig::GRI_SWEEP_WINDOW_MS) continue;
            
            double move = mid - tick.mid;
            
            if (std::abs(move) >= GoldConfig::GRI_MIN_SWEEP) {
                double vel = std::abs(buffer_.calcVelocity(400));
                
                if (vel >= gri_vel_threshold_) {
                    gri_on_ = true;
                    gri_today_++;
                    gri_dir_ = (move > 0) ? 1 : -1;
                    gri_entry_ = mid;
                    gri_peak_ = mid;
                    gri_partial_taken_ = false;
                    break;
                }
            }
        }
    }
    
    static bool isGRISession(int hour) {
        return (hour >= 7 && hour <= 10) || 
               (hour >= 13 && hour <= 16) || 
               (hour >= 19 && hour <= 20);
    }
    
private:
    TickBuffer buffer_;
    
    int64_t current_day_ = 0;
    double daily_pnl_ = 0.0;
    
    // MR state
    bool mr_on_ = false;
    int mr_dir_ = 0;
    double mr_entry_ = 0.0;
    int64_t mr_cooldown_ = 0;
    double mr_peak_vel_ = 0.0;
    int mr_peak_dir_ = 0;
    int64_t mr_peak_ts_ = 0;
    
    // SF state
    bool sf_on_ = false;
    int sf_dir_ = 0;
    double sf_entry_ = 0.0;
    int64_t sf_sweep_ts_ = 0;
    double sf_sweep_px_ = 0.0;
    int sf_today_ = 0;
    
    // SRM state
    bool srm_on_ = false;
    int srm_dir_ = 0;
    double srm_entry_ = 0.0;
    double srm_size_mult_ = 1.0;
    bool srm_sweep_detected_ = false;
    int srm_sweep_dir_ = 0;
    int64_t srm_sweep_ts_ = 0;
    double srm_sweep_level_ = 0.0;
    double srm_sweep_dist_ = 0.0;
    int srm_today_ = 0;
    double srm_daily_pnl_ = 0.0;
    bool srm_disabled_today_ = false;
    
    // GRI state
    bool gri_on_ = false;
    int gri_dir_ = 0;
    double gri_entry_ = 0.0;
    bool gri_partial_taken_ = false;
    double gri_peak_ = 0.0;
    int gri_today_ = 0;
    double gri_vel_threshold_ = 1.0;
};

} // namespace gold
