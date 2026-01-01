#pragma once
// =============================================================================
// FeeLossGuard.hpp - Detects fee-dominated losses
// =============================================================================
// v4.9.8: Tracks trades where gross > 0 but net < 0 (fee-only losses)
//
// PROBLEM: Trade has positive gross edge but fees destroy it
// SOLUTION: If 3+ fee-only losses in 10 trades → reduce size, force maker
// =============================================================================

#include <deque>
#include <cstdint>

namespace Chimera {
namespace Crypto {

struct FeeTradeRecord {
    double gross_pnl_bps;
    double net_pnl_bps;
    uint64_t ts_ns;
    bool was_taker_entry;
};

class FeeLossGuard {
public:
    explicit FeeLossGuard(int window_size = 10, int max_fee_losses = 3)
        : window_size_(window_size)
        , max_fee_losses_(max_fee_losses) {}
    
    // Record a trade and check if fee-loss guard should trigger
    bool recordTrade(double gross_pnl_bps, double net_pnl_bps, 
                     uint64_t ts_ns, bool was_taker_entry) {
        
        FeeTradeRecord rec{gross_pnl_bps, net_pnl_bps, ts_ns, was_taker_entry};
        trades_.push_back(rec);
        
        // Maintain window size
        while (trades_.size() > static_cast<size_t>(window_size_)) {
            trades_.pop_front();
        }
        
        return shouldTrigger();
    }
    
    // Count fee-only losses: gross > 0 but net < 0
    int feeOnlyLosses() const {
        int count = 0;
        for (const auto& t : trades_) {
            if (t.gross_pnl_bps > 0 && t.net_pnl_bps < 0) {
                count++;
            }
        }
        return count;
    }
    
    // Should the guard trigger?
    bool shouldTrigger() const {
        return feeOnlyLosses() >= max_fee_losses_;
    }
    
    // What percentage of losses are fee-dominated?
    double feeOnlyRatio() const {
        if (trades_.empty()) return 0.0;
        int losses = 0;
        int fee_losses = 0;
        for (const auto& t : trades_) {
            if (t.net_pnl_bps < 0) {
                losses++;
                if (t.gross_pnl_bps > 0) fee_losses++;
            }
        }
        return losses > 0 ? static_cast<double>(fee_losses) / losses : 0.0;
    }
    
    // What percentage of fee-losses used taker entry?
    double takerFeeRatio() const {
        int fee_losses = 0;
        int taker_fee_losses = 0;
        for (const auto& t : trades_) {
            if (t.gross_pnl_bps > 0 && t.net_pnl_bps < 0) {
                fee_losses++;
                if (t.was_taker_entry) taker_fee_losses++;
            }
        }
        return fee_losses > 0 ? static_cast<double>(taker_fee_losses) / fee_losses : 0.0;
    }
    
    void reset() { trades_.clear(); }
    int tradeCount() const { return static_cast<int>(trades_.size()); }

private:
    int window_size_;
    int max_fee_losses_;
    std::deque<FeeTradeRecord> trades_;
};

// =============================================================================
// TakerContaminationGuard - Detects excessive taker usage
// =============================================================================
// If taker ratio exceeds threshold, force maker-only mode
//
// Per-symbol thresholds:
//   BTC: >5% taker → force maker (speed edge destroyed)
//   ETH: >18% taker → force maker
//   SOL: >25% taker → force maker
// =============================================================================

class TakerContaminationGuard {
public:
    explicit TakerContaminationGuard(double taker_threshold = 0.20)
        : taker_threshold_(taker_threshold) {}
    
    void recordFill(bool is_taker) {
        total_fills_++;
        if (is_taker) taker_fills_++;
    }
    
    double takerRatio() const {
        return total_fills_ > 0 
            ? static_cast<double>(taker_fills_) / total_fills_ 
            : 0.0;
    }
    
    bool isContaminated() const {
        // Need minimum samples
        if (total_fills_ < 5) return false;
        return takerRatio() > taker_threshold_;
    }
    
    bool shouldForceMaker() const {
        return isContaminated();
    }
    
    void setThreshold(double thresh) { taker_threshold_ = thresh; }
    double threshold() const { return taker_threshold_; }
    
    uint32_t totalFills() const { return total_fills_; }
    uint32_t takerFills() const { return taker_fills_; }
    uint32_t makerFills() const { return total_fills_ - taker_fills_; }
    
    void reset() {
        total_fills_ = 0;
        taker_fills_ = 0;
    }

private:
    double taker_threshold_;
    uint32_t total_fills_ = 0;
    uint32_t taker_fills_ = 0;
};

// =============================================================================
// DynamicMakerTimeout - Adjusts timeout based on fill rate
// =============================================================================
// timeout = base + (queue_pos * 18) - (vol * 22) + (fill_deficit * 40)
//
// Symbol defaults:
//   BTC: base=220, min=160, max=420, target_fill=70%
//   ETH: base=160, min=120, max=300, target_fill=55%
//   SOL: base=100, min=80, max=200, target_fill=40%
// =============================================================================

struct MakerTimeoutConfig {
    int base_ms;
    int min_ms;
    int max_ms;
    double target_fill_rate;
};

constexpr MakerTimeoutConfig BTC_TIMEOUT_CONFIG = {220, 160, 420, 0.70};
constexpr MakerTimeoutConfig ETH_TIMEOUT_CONFIG = {160, 120, 300, 0.55};
constexpr MakerTimeoutConfig SOL_TIMEOUT_CONFIG = {100, 80, 200, 0.40};

class DynamicMakerTimeout {
public:
    explicit DynamicMakerTimeout(const MakerTimeoutConfig& cfg)
        : config_(cfg) 
        , current_timeout_ms_(cfg.base_ms) {}
    
    // Calculate timeout based on current conditions
    int calculate(double queue_position, double volatility, double current_fill_rate) {
        // Fill deficit: how far below target are we?
        double fill_deficit = std::max(0.0, config_.target_fill_rate - current_fill_rate);
        
        // Adjustment factors
        int queue_adj = static_cast<int>(queue_position * 18.0);
        int vol_adj = static_cast<int>(volatility * 22.0);
        int deficit_adj = static_cast<int>(fill_deficit * 40.0);
        
        // Calculate new timeout
        int timeout = config_.base_ms + queue_adj - vol_adj + deficit_adj;
        
        // Clamp to bounds
        timeout = std::max(config_.min_ms, std::min(config_.max_ms, timeout));
        
        current_timeout_ms_ = timeout;
        return timeout;
    }
    
    // Simple adaptive version (no queue info)
    int adapt(double fill_rate) {
        double deficit = std::max(0.0, config_.target_fill_rate - fill_rate);
        int adj = static_cast<int>(deficit * 100.0);  // 100ms per 1% deficit
        
        int timeout = config_.base_ms + adj;
        timeout = std::max(config_.min_ms, std::min(config_.max_ms, timeout));
        
        current_timeout_ms_ = timeout;
        return timeout;
    }
    
    int currentTimeout() const { return current_timeout_ms_; }
    const MakerTimeoutConfig& config() const { return config_; }

private:
    MakerTimeoutConfig config_;
    int current_timeout_ms_;
};

// =============================================================================
// MissedMoveDetector - Detects when we cancel and price moves to TP
// =============================================================================
// If cancel → price moves TP within 300ms → increase timeout
// =============================================================================

class MissedMoveDetector {
public:
    void recordCancel(double price_at_cancel, double intended_tp_bps, uint64_t ts_ns) {
        last_cancel_price_ = price_at_cancel;
        last_cancel_tp_bps_ = intended_tp_bps;
        last_cancel_ts_ns_ = ts_ns;
        has_pending_cancel_ = true;
    }
    
    bool checkMissedMove(double current_price, uint64_t ts_ns) {
        if (!has_pending_cancel_) return false;
        
        // Check if within 300ms window
        constexpr uint64_t WINDOW_NS = 300'000'000ULL;
        if (ts_ns - last_cancel_ts_ns_ > WINDOW_NS) {
            has_pending_cancel_ = false;
            return false;
        }
        
        // Check if price moved to what would have been TP
        double move_bps = (current_price - last_cancel_price_) / last_cancel_price_ * 10000.0;
        if (std::fabs(move_bps) >= last_cancel_tp_bps_) {
            missed_moves_++;
            has_pending_cancel_ = false;
            return true;
        }
        
        return false;
    }
    
    uint32_t missedMoves() const { return missed_moves_; }
    void reset() { 
        missed_moves_ = 0; 
        has_pending_cancel_ = false;
    }

private:
    double last_cancel_price_ = 0.0;
    double last_cancel_tp_bps_ = 0.0;
    uint64_t last_cancel_ts_ns_ = 0;
    bool has_pending_cancel_ = false;
    uint32_t missed_moves_ = 0;
};

} // namespace Crypto
} // namespace Chimera
