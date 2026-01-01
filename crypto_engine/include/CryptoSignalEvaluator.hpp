#pragma once
// =============================================================================
// CRYPTO SIGNAL EVALUATOR - Entry Gate Logic
// =============================================================================
// ALL gates must pass for a trade signal to be generated.
// This is where co-location edge is exploited.
// =============================================================================

#include <string>
#include <cmath>
#include <chrono>
#include <atomic>
#include <array>
#include <algorithm>

namespace Chimera::Crypto {

// =============================================================================
// Signal Output Structure
// =============================================================================
struct CryptoSignal {
    bool near_trigger = false;      // Informational: conditions forming
    bool ready_to_trade = false;    // All gates passed
    std::string side;               // "BUY" or "SELL"
    double imbalance = 0.0;         // Current order book imbalance
    double stop_px = 0.0;           // Micro-structure stop price
    double target_px = 0.0;         // 1.5× stop target
    double entry_spread = 0.0;      // Spread at signal time (for exit monitoring)
    const char* block_reason = "";  // Why signal was blocked (if not ready)
};

// =============================================================================
// Gate Thresholds (HARDCODED - NO TUNING)
// =============================================================================
namespace SignalConstants {
    // Spread Gate
    constexpr double SPREAD_COMPRESSION_MULT = 0.6;     // Must be ≤ 60% of median
    
    // Volatility Gate
    constexpr double VOL_PERCENTILE_MAX = 0.35;         // Below 35th percentile
    
    // Order Book Imbalance Gate
    constexpr double IMB_NEAR_THRESHOLD = 1.20;         // Near trigger (informational)
    constexpr double IMB_TRADE_THRESHOLD = 1.35;        // Trade threshold
    constexpr double IMB_EXIT_THRESHOLD = 1.10;         // Exit if drops below
    
    // Price Acceptance Gate (Anti-Spoof)
    constexpr int64_t ACCEPTANCE_HOLD_MS = 400;         // Must hold 400ms
    constexpr int64_t NO_SWEEP_WINDOW_MS = 300;         // No opposing sweep in 300ms
    
    // Position Parameters
    constexpr double STOP_BPS = 5.0;                    // 5 bps micro-stop
    constexpr double TARGET_MULT = 1.5;                 // 1.5× stop
    
    // Rolling Window Sizes
    constexpr size_t SPREAD_WINDOW = 1440;              // 24h of minute samples
    constexpr size_t VOL_WINDOW = 1440;                 // 24h of minute samples
}

// =============================================================================
// Time-of-Day Filter (Macro Risk Windows)
// =============================================================================
class TimeOfDayFilter {
public:
    static bool isRiskWindow() noexcept {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm* utc = std::gmtime(&time_t_now);
        int hour = utc->tm_hour;
        int minute = utc->tm_min;
        int total_mins = hour * 60 + minute;
        
        // US Equity Open overlap (13:30-15:30 UTC) - liquidation risk
        if (total_mins >= 810 && total_mins <= 930) return true;
        
        // Major macro news windows (typically 12:30, 14:00, 18:00 UTC)
        // Be conservative: block 30 min before/after
        if (total_mins >= 720 && total_mins <= 780) return true;   // 12:00-13:00 UTC
        if (total_mins >= 810 && total_mins <= 870) return true;   // 13:30-14:30 UTC
        if (total_mins >= 1050 && total_mins <= 1110) return true; // 17:30-18:30 UTC
        
        return false;
    }
};

// =============================================================================
// Rolling Statistics Tracker
// =============================================================================
class RollingStats {
private:
    std::array<double, SignalConstants::SPREAD_WINDOW> spread_samples_{};
    std::array<double, SignalConstants::VOL_WINDOW> vol_samples_{};
    size_t spread_idx_ = 0;
    size_t vol_idx_ = 0;
    size_t spread_count_ = 0;
    size_t vol_count_ = 0;
    
    double last_price_ = 0.0;
    int64_t last_price_ts_ = 0;
    
public:
    void recordSpread(double spread) noexcept {
        spread_samples_[spread_idx_] = spread;
        spread_idx_ = (spread_idx_ + 1) % SignalConstants::SPREAD_WINDOW;
        if (spread_count_ < SignalConstants::SPREAD_WINDOW) spread_count_++;
    }
    
    void recordPrice(double price, int64_t now_ms) noexcept {
        if (last_price_ > 0 && now_ms > last_price_ts_) {
            double ret = std::log(price / last_price_);
            double vol = std::abs(ret) * 100.0;  // Scale to percentage
            vol_samples_[vol_idx_] = vol;
            vol_idx_ = (vol_idx_ + 1) % SignalConstants::VOL_WINDOW;
            if (vol_count_ < SignalConstants::VOL_WINDOW) vol_count_++;
        }
        last_price_ = price;
        last_price_ts_ = now_ms;
    }
    
    double medianSpread() const noexcept {
        if (spread_count_ < 10) return 1e9;  // Not enough data
        
        std::array<double, SignalConstants::SPREAD_WINDOW> sorted;
        std::copy(spread_samples_.begin(), spread_samples_.begin() + spread_count_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + spread_count_);
        return sorted[spread_count_ / 2];
    }
    
    double volPercentile(double current_vol) const noexcept {
        if (vol_count_ < 10) return 1.0;  // Not enough data, assume high
        
        size_t below = 0;
        for (size_t i = 0; i < vol_count_; i++) {
            if (vol_samples_[i] < current_vol) below++;
        }
        return static_cast<double>(below) / vol_count_;
    }
    
    double currentVol() const noexcept {
        if (vol_count_ == 0) return 0.0;
        // Return most recent vol sample
        size_t idx = (vol_idx_ == 0) ? SignalConstants::VOL_WINDOW - 1 : vol_idx_ - 1;
        return vol_samples_[idx];
    }
    
    bool hasEnoughData() const noexcept {
        return spread_count_ >= 60 && vol_count_ >= 60;  // At least 1 hour
    }
};

// =============================================================================
// Price Acceptance Tracker (Anti-Spoof)
// =============================================================================
class AcceptanceTracker {
private:
    double vwap_ = 0.0;
    double cum_vol_ = 0.0;
    double cum_pv_ = 0.0;
    
    int64_t long_accept_start_ = 0;
    int64_t short_accept_start_ = 0;
    int64_t last_buy_sweep_ts_ = 0;
    int64_t last_sell_sweep_ts_ = 0;
    
public:
    void updateVWAP(double price, double volume, int64_t now_ms) noexcept {
        cum_pv_ += price * volume;
        cum_vol_ += volume;
        if (cum_vol_ > 0) {
            vwap_ = cum_pv_ / cum_vol_;
        }
        
        // Track price acceptance
        if (price > vwap_) {
            if (long_accept_start_ == 0) long_accept_start_ = now_ms;
            short_accept_start_ = 0;
        } else if (price < vwap_) {
            if (short_accept_start_ == 0) short_accept_start_ = now_ms;
            long_accept_start_ = 0;
        } else {
            long_accept_start_ = 0;
            short_accept_start_ = 0;
        }
    }
    
    void recordSweep(bool is_buy, int64_t now_ms) noexcept {
        if (is_buy) {
            last_buy_sweep_ts_ = now_ms;
        } else {
            last_sell_sweep_ts_ = now_ms;
        }
    }
    
    double vwap() const noexcept { return vwap_; }
    
    bool longAccepted(int64_t now_ms) const noexcept {
        if (long_accept_start_ == 0) return false;
        return (now_ms - long_accept_start_) >= SignalConstants::ACCEPTANCE_HOLD_MS;
    }
    
    bool shortAccepted(int64_t now_ms) const noexcept {
        if (short_accept_start_ == 0) return false;
        return (now_ms - short_accept_start_) >= SignalConstants::ACCEPTANCE_HOLD_MS;
    }
    
    bool recentOpposingSweep(bool for_long, int64_t now_ms) const noexcept {
        if (for_long) {
            return (now_ms - last_sell_sweep_ts_) < SignalConstants::NO_SWEEP_WINDOW_MS;
        } else {
            return (now_ms - last_buy_sweep_ts_) < SignalConstants::NO_SWEEP_WINDOW_MS;
        }
    }
    
    void resetDaily() noexcept {
        vwap_ = 0.0;
        cum_vol_ = 0.0;
        cum_pv_ = 0.0;
        long_accept_start_ = 0;
        short_accept_start_ = 0;
    }
};

// =============================================================================
// Main Signal Evaluator
// =============================================================================
class CryptoSignalEvaluator {
private:
    RollingStats stats_btc_;
    RollingStats stats_eth_;
    AcceptanceTracker accept_btc_;
    AcceptanceTracker accept_eth_;
    
    RollingStats& getStats(const char* symbol) noexcept {
        if (symbol[0] == 'B') return stats_btc_;
        return stats_eth_;
    }
    
    AcceptanceTracker& getAccept(const char* symbol) noexcept {
        if (symbol[0] == 'B') return accept_btc_;
        return accept_eth_;
    }

public:
    // Update market data (call on every tick)
    void onTick(const char* symbol, double price, double spread, 
                double volume, int64_t now_ms) noexcept {
        auto& stats = getStats(symbol);
        auto& accept = getAccept(symbol);
        
        stats.recordSpread(spread);
        stats.recordPrice(price, now_ms);
        accept.updateVWAP(price, volume, now_ms);
    }
    
    // Record aggressive sweep (for anti-spoof detection)
    void onSweep(const char* symbol, bool is_buy, int64_t now_ms) noexcept {
        getAccept(symbol).recordSweep(is_buy, now_ms);
    }
    
    // Evaluate signal (call when considering entry)
    CryptoSignal evaluate(const char* symbol,
                          double price,
                          double spread,
                          double bid_vol_5,
                          double ask_vol_5,
                          int64_t now_ms) noexcept {
        CryptoSignal sig;
        sig.entry_spread = spread;
        
        auto& stats = getStats(symbol);
        auto& accept = getAccept(symbol);
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 0: Data Readiness
        // ═══════════════════════════════════════════════════════════════
        if (!stats.hasEnoughData()) {
            sig.block_reason = "WARMUP";
            return sig;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 1: Time-of-Day Filter
        // ═══════════════════════════════════════════════════════════════
        if (TimeOfDayFilter::isRiskWindow()) {
            sig.block_reason = "MACRO_WINDOW";
            return sig;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 2: Spread Compression
        // ═══════════════════════════════════════════════════════════════
        double median_spread = stats.medianSpread();
        if (spread > median_spread * SignalConstants::SPREAD_COMPRESSION_MULT) {
            sig.block_reason = "SPREAD_WIDE";
            return sig;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 3: Volatility Regime
        // ═══════════════════════════════════════════════════════════════
        double current_vol = stats.currentVol();
        double vol_pct = stats.volPercentile(current_vol);
        if (vol_pct > SignalConstants::VOL_PERCENTILE_MAX) {
            sig.block_reason = "VOL_HIGH";
            return sig;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 4: Order Book Imbalance
        // ═══════════════════════════════════════════════════════════════
        if (ask_vol_5 <= 0 || bid_vol_5 <= 0) {
            sig.block_reason = "NO_DEPTH";
            return sig;
        }
        
        double long_imb = bid_vol_5 / ask_vol_5;
        double short_imb = ask_vol_5 / bid_vol_5;
        sig.imbalance = std::max(long_imb, short_imb);
        
        bool long_bias = long_imb >= SignalConstants::IMB_TRADE_THRESHOLD;
        bool short_bias = short_imb >= SignalConstants::IMB_TRADE_THRESHOLD;
        
        // Near trigger (informational)
        if (long_imb >= SignalConstants::IMB_NEAR_THRESHOLD || 
            short_imb >= SignalConstants::IMB_NEAR_THRESHOLD) {
            sig.near_trigger = true;
        }
        
        if (!long_bias && !short_bias) {
            sig.block_reason = "IMB_LOW";
            return sig;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 5: Price Acceptance (Anti-Spoof)
        // ═══════════════════════════════════════════════════════════════
        if (long_bias) {
            if (!accept.longAccepted(now_ms)) {
                sig.block_reason = "ACCEPT_WAIT";
                return sig;
            }
            if (accept.recentOpposingSweep(true, now_ms)) {
                sig.block_reason = "SWEEP_BLOCK";
                return sig;
            }
        } else {
            if (!accept.shortAccepted(now_ms)) {
                sig.block_reason = "ACCEPT_WAIT";
                return sig;
            }
            if (accept.recentOpposingSweep(false, now_ms)) {
                sig.block_reason = "SWEEP_BLOCK";
                return sig;
            }
        }
        
        // ═══════════════════════════════════════════════════════════════
        // ALL GATES PASSED - Generate Signal
        // ═══════════════════════════════════════════════════════════════
        sig.ready_to_trade = true;
        
        if (long_bias) {
            sig.side = "BUY";
            double stop_dist = price * (SignalConstants::STOP_BPS / 10000.0);
            sig.stop_px = price - stop_dist;
            sig.target_px = price + (stop_dist * SignalConstants::TARGET_MULT);
        } else {
            sig.side = "SELL";
            double stop_dist = price * (SignalConstants::STOP_BPS / 10000.0);
            sig.stop_px = price + stop_dist;
            sig.target_px = price - (stop_dist * SignalConstants::TARGET_MULT);
        }
        
        return sig;
    }
    
    // Check if position should exit (call while holding)
    bool shouldExit(const char* symbol, double price, double entry_spread,
                    double current_spread, double bid_vol_5, double ask_vol_5,
                    bool is_long, int64_t entry_time_ms, int64_t now_ms) const noexcept {
        // Exit 1: Time in trade > 6 seconds
        if (now_ms - entry_time_ms > 6000) return true;
        
        // Exit 2: Spread widens > 1.2× entry spread
        if (current_spread > entry_spread * 1.2) return true;
        
        // Exit 3: Imbalance collapses
        if (ask_vol_5 > 0 && bid_vol_5 > 0) {
            double imb = is_long ? (bid_vol_5 / ask_vol_5) : (ask_vol_5 / bid_vol_5);
            if (imb < SignalConstants::IMB_EXIT_THRESHOLD) return true;
        }
        
        // Exit 4: Price re-enters VWAP zone
        const auto& accept = symbol[0] == 'B' ? accept_btc_ : accept_eth_;
        double vwap = accept.vwap();
        if (vwap > 0) {
            if (is_long && price <= vwap) return true;
            if (!is_long && price >= vwap) return true;
        }
        
        return false;
    }
    
    // Daily reset
    void resetDaily() noexcept {
        accept_btc_.resetDaily();
        accept_eth_.resetDaily();
    }
};

} // namespace Chimera::Crypto
