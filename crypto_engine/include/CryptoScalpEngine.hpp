#pragma once
// =============================================================================
// CryptoScalpEngine.hpp - Chimera v2.3.3 Institutional Crypto Scalping Engine
// =============================================================================
// Complete state machine for ultra-low latency crypto market making
// Co-located NY VPS (45.85.3.38) with 0.2ms to Binance WebSocket
// NEVER use REST API for orders - WebSocket only for speed edge
// =============================================================================

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace chimera {
namespace crypto {

// =============================================================================
// Forward declarations
// =============================================================================
class CryptoScalpEngine;
class CryptoScalpManager;

// =============================================================================
// Core Types and Enums
// =============================================================================

enum class CryptoState : uint8_t {
    IDLE = 0,
    QUOTING,
    PENDING_FILL,
    IN_POSITION,
    EXITING,
    COOLDOWN,
    HALTED,
    ERROR
};

enum class CryptoSide : uint8_t {
    NONE = 0,
    BID,
    ASK
};

inline const char* state_to_string(CryptoState s) noexcept {
    switch (s) {
        case CryptoState::IDLE:         return "IDLE";
        case CryptoState::QUOTING:      return "QUOTING";
        case CryptoState::PENDING_FILL: return "PENDING_FILL";
        case CryptoState::IN_POSITION:  return "IN_POSITION";
        case CryptoState::EXITING:      return "EXITING";
        case CryptoState::COOLDOWN:     return "COOLDOWN";
        case CryptoState::HALTED:       return "HALTED";
        case CryptoState::ERROR:        return "ERROR";
        default:                        return "UNKNOWN";
    }
}

inline const char* side_to_string(CryptoSide s) noexcept {
    switch (s) {
        case CryptoSide::NONE: return "NONE";
        case CryptoSide::BID:  return "BID";
        case CryptoSide::ASK:  return "ASK";
        default:               return "UNKNOWN";
    }
}

// =============================================================================
// CryptoParams - Core trading parameters with safe defaults
// =============================================================================

struct CryptoParams {
    // Position sizing
    double base_size;
    double max_position;
    double max_exposure_usd;
    
    // Spread and depth thresholds
    double min_spread_bps;
    double min_depth_usd;
    
    // Toxicity thresholds
    double max_toxic_flow;
    double max_vpin;
    
    // Risk limits
    double stop_loss_bps;
    double take_profit_bps;
    
    // Timing
    uint64_t cooldown_ms;
    uint64_t max_latency_us;
    uint64_t quote_lifetime_ms;
    
    // Maker queue
    double min_queue_priority;
    uint32_t max_requotes;
    
    // Constructor with safe defaults
    CryptoParams()
        : base_size(0.0005)
        , max_position(1.0)
        , max_exposure_usd(50.0)
        , min_spread_bps(4.0)
        , min_depth_usd(30000.0)
        , max_toxic_flow(0.55)
        , max_vpin(0.60)
        , stop_loss_bps(25.0)
        , take_profit_bps(45.0)
        , cooldown_ms(250)
        , max_latency_us(600)
        , quote_lifetime_ms(100)
        , min_queue_priority(0.3)
        , max_requotes(3)
    {}
};

// =============================================================================
// CryptoTick - Unified tick structure (192 bytes for cache alignment)
// =============================================================================

struct alignas(64) CryptoTick {
    uint64_t exchange_ts;
    uint64_t local_ts;
    double bid_px;
    double ask_px;
    double bid_sz;
    double ask_sz;
    double last_px;
    double last_sz;
    double bid_depth_5;
    double ask_depth_5;
    double vpin;
    double toxic_flow;
    double imbalance;
    uint64_t trade_count;
    uint32_t sequence;
    uint8_t flags;
    uint8_t _pad[3];
    
    CryptoTick()
        : exchange_ts(0)
        , local_ts(0)
        , bid_px(0.0)
        , ask_px(0.0)
        , bid_sz(0.0)
        , ask_sz(0.0)
        , last_px(0.0)
        , last_sz(0.0)
        , bid_depth_5(0.0)
        , ask_depth_5(0.0)
        , vpin(0.0)
        , toxic_flow(0.0)
        , imbalance(0.0)
        , trade_count(0)
        , sequence(0)
        , flags(0)
        , _pad{0, 0, 0}
    {}
    
    double mid() const noexcept {
        return (bid_px + ask_px) * 0.5;
    }
    
    double spread_bps() const noexcept {
        double m = mid();
        return m > 0.0 ? ((ask_px - bid_px) / m) * 10000.0 : 0.0;
    }
    
    bool is_valid() const noexcept {
        return bid_px > 0.0 && ask_px > 0.0 && ask_px > bid_px;
    }
};

static_assert(sizeof(CryptoTick) <= 192, "CryptoTick exceeds 192 bytes");

// =============================================================================
// SpoofingDetector - Detects order book manipulation
// =============================================================================

class SpoofingDetector {
public:
    struct Config {
        double depth_change_threshold;
        double time_decay_factor;
        uint32_t window_size;
        double spoof_score_threshold;
        
        Config()
            : depth_change_threshold(0.3)
            , time_decay_factor(0.95)
            , window_size(20)
            , spoof_score_threshold(0.7)
        {}
    };
    
    explicit SpoofingDetector(const Config& cfg = Config())
        : cfg_(cfg)
        , spoof_score_(0.0)
        , last_bid_depth_(0.0)
        , last_ask_depth_(0.0)
        , sample_count_(0)
    {}
    
    void update(double bid_depth, double ask_depth, uint64_t ts) noexcept {
        if (sample_count_ > 0) {
            double bid_change = std::abs(bid_depth - last_bid_depth_) / 
                               (last_bid_depth_ > 0 ? last_bid_depth_ : 1.0);
            double ask_change = std::abs(ask_depth - last_ask_depth_) / 
                               (last_ask_depth_ > 0 ? last_ask_depth_ : 1.0);
            
            double instantaneous = std::max(bid_change, ask_change);
            
            if (instantaneous > cfg_.depth_change_threshold) {
                spoof_score_ = spoof_score_ * cfg_.time_decay_factor + 
                              (1.0 - cfg_.time_decay_factor) * instantaneous;
            } else {
                spoof_score_ *= cfg_.time_decay_factor;
            }
        }
        
        last_bid_depth_ = bid_depth;
        last_ask_depth_ = ask_depth;
        last_ts_ = ts;
        ++sample_count_;
    }
    
    bool is_spoofing_detected() const noexcept {
        return spoof_score_ > cfg_.spoof_score_threshold;
    }
    
    double score() const noexcept { return spoof_score_; }
    void reset() noexcept { spoof_score_ = 0.0; sample_count_ = 0; }

private:
    Config cfg_;
    double spoof_score_;
    double last_bid_depth_;
    double last_ask_depth_;
    uint64_t last_ts_{0};
    uint32_t sample_count_;
};

// =============================================================================
// AdaptiveStability - Tracks market regime stability
// =============================================================================

class AdaptiveStability {
public:
    struct Config {
        uint32_t window_size;
        double stability_threshold;
        double volatility_weight;
        double spread_weight;
        
        Config()
            : window_size(50)
            , stability_threshold(0.6)
            , volatility_weight(0.4)
            , spread_weight(0.6)
        {}
    };
    
    explicit AdaptiveStability(const Config& cfg = Config())
        : cfg_(cfg)
        , stability_score_(1.0)
        , volatility_ema_(0.0)
        , spread_ema_(0.0)
        , sample_count_(0)
    {}
    
    void update(double mid, double spread_bps) noexcept {
        if (sample_count_ > 0 && last_mid_ > 0.0) {
            double ret = (mid - last_mid_) / last_mid_;
            double vol = std::abs(ret) * 10000.0;
            
            double alpha = 2.0 / (cfg_.window_size + 1);
            volatility_ema_ = alpha * vol + (1.0 - alpha) * volatility_ema_;
            spread_ema_ = alpha * spread_bps + (1.0 - alpha) * spread_ema_;
            
            double vol_component = 1.0 / (1.0 + volatility_ema_ / 10.0);
            double spread_component = 1.0 / (1.0 + spread_ema_ / 20.0);
            
            stability_score_ = cfg_.volatility_weight * vol_component +
                              cfg_.spread_weight * spread_component;
        }
        
        last_mid_ = mid;
        ++sample_count_;
    }
    
    bool is_stable() const noexcept {
        return stability_score_ >= cfg_.stability_threshold;
    }
    
    double score() const noexcept { return stability_score_; }
    double volatility() const noexcept { return volatility_ema_; }
    void reset() noexcept { stability_score_ = 1.0; volatility_ema_ = 0.0; sample_count_ = 0; }

private:
    Config cfg_;
    double stability_score_;
    double volatility_ema_;
    double spread_ema_;
    double last_mid_{0.0};
    uint32_t sample_count_;
};

// =============================================================================
// LatencyAdaptiveSizer - Adjusts size based on latency conditions
// =============================================================================

class LatencyAdaptiveSizer {
public:
    struct Config {
        uint64_t target_latency_us;
        uint64_t max_latency_us;
        double min_size_multiplier;
        double max_size_multiplier;
        uint32_t ema_window;
        
        Config()
            : target_latency_us(200)
            , max_latency_us(600)
            , min_size_multiplier(0.25)
            , max_size_multiplier(1.5)
            , ema_window(20)
        {}
    };
    
    explicit LatencyAdaptiveSizer(const Config& cfg = Config())
        : cfg_(cfg)
        , latency_ema_(static_cast<double>(cfg.target_latency_us))
        , current_multiplier_(1.0)
    {}
    
    void update(uint64_t latency_us) noexcept {
        double alpha = 2.0 / (cfg_.ema_window + 1);
        latency_ema_ = alpha * static_cast<double>(latency_us) + 
                      (1.0 - alpha) * latency_ema_;
        
        if (latency_ema_ <= static_cast<double>(cfg_.target_latency_us)) {
            double ratio = latency_ema_ / static_cast<double>(cfg_.target_latency_us);
            current_multiplier_ = cfg_.max_size_multiplier - 
                                 (cfg_.max_size_multiplier - 1.0) * ratio;
        } else if (latency_ema_ >= static_cast<double>(cfg_.max_latency_us)) {
            current_multiplier_ = cfg_.min_size_multiplier;
        } else {
            double range = static_cast<double>(cfg_.max_latency_us - cfg_.target_latency_us);
            double excess = latency_ema_ - static_cast<double>(cfg_.target_latency_us);
            double ratio = excess / range;
            current_multiplier_ = 1.0 - (1.0 - cfg_.min_size_multiplier) * ratio;
        }
    }
    
    double multiplier() const noexcept { return current_multiplier_; }
    double latency_ema() const noexcept { return latency_ema_; }
    bool is_latency_ok() const noexcept { 
        return latency_ema_ < static_cast<double>(cfg_.max_latency_us); 
    }

private:
    Config cfg_;
    double latency_ema_;
    double current_multiplier_;
};

// =============================================================================
// LatencySanity - Guards against stale data
// =============================================================================

class LatencySanity {
public:
    struct Config {
        uint64_t max_age_us;
        uint64_t max_clock_drift_us;
        uint32_t consecutive_failures_halt;
        
        Config()
            : max_age_us(5000)
            , max_clock_drift_us(50000)
            , consecutive_failures_halt(5)
        {}
    };
    
    explicit LatencySanity(const Config& cfg = Config())
        : cfg_(cfg)
        , consecutive_failures_(0)
        , total_failures_(0)
        , last_valid_ts_(0)
    {}
    
    bool check(uint64_t exchange_ts, uint64_t local_ts) noexcept {
        if (exchange_ts == 0 || local_ts == 0) {
            ++consecutive_failures_;
            ++total_failures_;
            return false;
        }
        
        uint64_t age = (local_ts > exchange_ts) ? (local_ts - exchange_ts) : 0;
        
        if (age > cfg_.max_age_us) {
            ++consecutive_failures_;
            ++total_failures_;
            return false;
        }
        
        if (exchange_ts < last_valid_ts_ && 
            (last_valid_ts_ - exchange_ts) > cfg_.max_clock_drift_us) {
            ++consecutive_failures_;
            ++total_failures_;
            return false;
        }
        
        consecutive_failures_ = 0;
        last_valid_ts_ = exchange_ts;
        return true;
    }
    
    bool should_halt() const noexcept {
        return consecutive_failures_ >= cfg_.consecutive_failures_halt;
    }
    
    uint32_t consecutive_failures() const noexcept { return consecutive_failures_; }
    uint64_t total_failures() const noexcept { return total_failures_; }
    void reset() noexcept { consecutive_failures_ = 0; }

private:
    Config cfg_;
    uint32_t consecutive_failures_;
    uint64_t total_failures_;
    uint64_t last_valid_ts_;
};

// =============================================================================
// MakerQueueEstimator - Estimates queue position for maker orders
// =============================================================================

class MakerQueueEstimator {
public:
    struct Config {
        double fill_rate_decay;
        uint32_t history_size;
        double min_queue_confidence;
        
        Config()
            : fill_rate_decay(0.9)
            , history_size(100)
            , min_queue_confidence(0.5)
        {}
    };
    
    explicit MakerQueueEstimator(const Config& cfg = Config())
        : cfg_(cfg)
        , estimated_position_(0.0)
        , fill_probability_(0.5)
        , depth_at_price_(0.0)
        , orders_ahead_(0.0)
    {}
    
    void set_order(double price, double size, double total_depth, 
                   CryptoSide side) noexcept {
        order_price_ = price;
        order_size_ = size;
        depth_at_price_ = total_depth;
        side_ = side;
        orders_ahead_ = total_depth * 0.5;
        estimated_position_ = orders_ahead_ / (total_depth > 0 ? total_depth : 1.0);
    }
    
    void update_depth(double new_depth, double trades_through) noexcept {
        if (trades_through > 0) {
            orders_ahead_ = std::max(0.0, orders_ahead_ - trades_through);
        }
        
        double depth_change = new_depth - depth_at_price_;
        if (depth_change < 0) {
            orders_ahead_ = std::max(0.0, orders_ahead_ + depth_change * 0.5);
        }
        
        depth_at_price_ = new_depth;
        estimated_position_ = (depth_at_price_ > 0) ? 
                             (orders_ahead_ / depth_at_price_) : 1.0;
        
        fill_probability_ = cfg_.fill_rate_decay * fill_probability_ +
                           (1.0 - cfg_.fill_rate_decay) * (1.0 - estimated_position_);
    }
    
    double queue_position() const noexcept { return estimated_position_; }
    double fill_prob() const noexcept { return fill_probability_; }
    bool is_favorable() const noexcept { 
        return estimated_position_ < 0.5 && fill_probability_ > cfg_.min_queue_confidence; 
    }

private:
    Config cfg_;
    double estimated_position_;
    double fill_probability_;
    double depth_at_price_;
    double orders_ahead_;
    double order_price_{0.0};
    double order_size_{0.0};
    CryptoSide side_{CryptoSide::NONE};
};

// =============================================================================
// AdaptiveProbeSizer - Dynamic position sizing based on market conditions
// =============================================================================

class AdaptiveProbeSizer {
public:
    struct Config {
        double base_size;
        double min_size;
        double max_size;
        double volatility_sensitivity;
        double spread_sensitivity;
        double depth_sensitivity;
        
        Config()
            : base_size(0.0005)
            , min_size(0.0001)
            , max_size(0.002)
            , volatility_sensitivity(0.5)
            , spread_sensitivity(0.3)
            , depth_sensitivity(0.2)
        {}
    };
    
    explicit AdaptiveProbeSizer(const Config& cfg = Config())
        : cfg_(cfg)
        , current_size_(cfg.base_size)
    {}
    
    double compute_size(double volatility, double spread_bps, 
                       double depth_usd, double stability) noexcept {
        double vol_factor = 1.0 / (1.0 + volatility * cfg_.volatility_sensitivity);
        
        double spread_factor = (spread_bps > 2.0) ? 
                              std::min(1.5, spread_bps / 4.0) : 0.5;
        spread_factor = 1.0 + (spread_factor - 1.0) * cfg_.spread_sensitivity;
        
        double depth_factor = std::min(2.0, depth_usd / 50000.0);
        depth_factor = 0.5 + depth_factor * cfg_.depth_sensitivity;
        
        double stability_factor = 0.5 + stability * 0.5;
        
        current_size_ = cfg_.base_size * vol_factor * spread_factor * 
                       depth_factor * stability_factor;
        
        current_size_ = std::clamp(current_size_, cfg_.min_size, cfg_.max_size);
        
        return current_size_;
    }
    
    double current() const noexcept { return current_size_; }
    void set_base(double base) noexcept { cfg_.base_size = base; }

private:
    Config cfg_;
    double current_size_;
};

// =============================================================================
// CrossSymbolRiskGuard - Portfolio-level risk management
// =============================================================================

class CrossSymbolRiskGuard {
public:
    struct Config {
        double max_total_exposure_usd;
        double max_correlation_exposure;
        uint32_t max_concurrent_positions;
        double drawdown_halt_pct;
        
        Config()
            : max_total_exposure_usd(500.0)
            , max_correlation_exposure(0.7)
            , max_concurrent_positions(3)
            , drawdown_halt_pct(5.0)
        {}
    };
    
    explicit CrossSymbolRiskGuard(const Config& cfg = Config())
        : cfg_(cfg)
        , total_exposure_(0.0)
        , position_count_(0)
        , peak_equity_(0.0)
        , current_equity_(0.0)
        , is_halted_(false)
    {}
    
    bool can_add_position(const std::string& symbol, double exposure_usd) noexcept {
        if (is_halted_) return false;
        if (position_count_ >= cfg_.max_concurrent_positions) return false;
        if ((total_exposure_ + exposure_usd) > cfg_.max_total_exposure_usd) return false;
        return true;
    }
    
    void add_position(const std::string& symbol, double exposure_usd) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        exposures_[symbol] = exposure_usd;
        recalculate();
    }
    
    void remove_position(const std::string& symbol) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        exposures_.erase(symbol);
        recalculate();
    }
    
    void update_pnl(double realized_pnl) noexcept {
        current_equity_ += realized_pnl;
        peak_equity_ = std::max(peak_equity_, current_equity_);
        
        if (peak_equity_ > 0) {
            double drawdown = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
            if (drawdown >= cfg_.drawdown_halt_pct) {
                is_halted_ = true;
            }
        }
    }
    
    bool is_halted() const noexcept { return is_halted_; }
    double total_exposure() const noexcept { return total_exposure_; }
    uint32_t position_count() const noexcept { return position_count_; }
    double drawdown_pct() const noexcept {
        return peak_equity_ > 0 ? 
               (peak_equity_ - current_equity_) / peak_equity_ * 100.0 : 0.0;
    }
    
    void reset_halt() noexcept { is_halted_ = false; }
    void set_initial_equity(double equity) noexcept { 
        peak_equity_ = current_equity_ = equity; 
    }

private:
    void recalculate() noexcept {
        total_exposure_ = 0.0;
        position_count_ = 0;
        for (const auto& [sym, exp] : exposures_) {
            total_exposure_ += exp;
            ++position_count_;
        }
    }
    
    Config cfg_;
    std::unordered_map<std::string, double> exposures_;
    std::mutex mtx_;
    double total_exposure_;
    uint32_t position_count_;
    double peak_equity_;
    double current_equity_;
    bool is_halted_;
};

// =============================================================================
// CryptoScalpEngine - Main state machine for single symbol
// =============================================================================

class CryptoScalpEngine {
public:
    struct Config {
        CryptoParams params;
        SpoofingDetector::Config spoofing;
        AdaptiveStability::Config stability;
        LatencyAdaptiveSizer::Config latency_sizer;
        LatencySanity::Config latency_sanity;
        MakerQueueEstimator::Config queue_estimator;
        AdaptiveProbeSizer::Config probe_sizer;
        bool enable_logging;
        
        Config()
            : params()
            , spoofing()
            , stability()
            , latency_sizer()
            , latency_sanity()
            , queue_estimator()
            , probe_sizer()
            , enable_logging(true)
        {}
    };
    
    using OrderCallback = std::function<void(const std::string& symbol, 
                                             CryptoSide side, 
                                             double price, 
                                             double size,
                                             bool is_cancel)>;
    
    explicit CryptoScalpEngine(const std::string& symbol, 
                               const Config& cfg = Config())
        : symbol_(symbol)
        , cfg_(cfg)
        , spoofing_(cfg.spoofing)
        , stability_(cfg.stability)
        , latency_sizer_(cfg.latency_sizer)
        , latency_sanity_(cfg.latency_sanity)
        , queue_estimator_(cfg.queue_estimator)
        , probe_sizer_(cfg.probe_sizer)
        , state_(CryptoState::IDLE)
        , position_side_(CryptoSide::NONE)
        , position_size_(0.0)
        , position_price_(0.0)
        , quote_price_(0.0)
        , quote_side_(CryptoSide::NONE)
        , realized_pnl_(0.0)
        , unrealized_pnl_(0.0)
        , trade_count_(0)
        , win_count_(0)
        , cooldown_until_(0)
        , last_quote_ts_(0)
        , requote_count_(0)
    {}
    
    // -------------------------------------------------------------------------
    // Main tick processing - called on every book update
    // -------------------------------------------------------------------------
    void on_tick(const CryptoTick& tick) noexcept {
        // Latency sanity check
        if (!latency_sanity_.check(tick.exchange_ts, tick.local_ts)) {
            if (latency_sanity_.should_halt()) {
                transition_to(CryptoState::HALTED);
            }
            return;
        }
        
        // Update all analyzers
        spoofing_.update(tick.bid_depth_5, tick.ask_depth_5, tick.local_ts);
        stability_.update(tick.mid(), tick.spread_bps());
        latency_sizer_.update(tick.local_ts - tick.exchange_ts);
        
        // Store current tick for reference
        last_tick_ = tick;
        
        // State machine
        switch (state_) {
            case CryptoState::IDLE:
                handle_idle(tick);
                break;
            case CryptoState::QUOTING:
                handle_quoting(tick);
                break;
            case CryptoState::PENDING_FILL:
                handle_pending_fill(tick);
                break;
            case CryptoState::IN_POSITION:
                handle_in_position(tick);
                break;
            case CryptoState::EXITING:
                handle_exiting(tick);
                break;
            case CryptoState::COOLDOWN:
                handle_cooldown(tick);
                break;
            case CryptoState::HALTED:
            case CryptoState::ERROR:
                break;
        }
    }
    
    // -------------------------------------------------------------------------
    // Order fill notification
    // -------------------------------------------------------------------------
    void on_fill(CryptoSide side, double price, double size) noexcept {
        if (state_ == CryptoState::PENDING_FILL || state_ == CryptoState::QUOTING) {
            position_side_ = side;
            position_price_ = price;
            position_size_ = size;
            transition_to(CryptoState::IN_POSITION);
        } else if (state_ == CryptoState::EXITING) {
            double exit_price = price;
            double pnl = 0.0;
            
            if (position_side_ == CryptoSide::BID) {
                pnl = (exit_price - position_price_) * position_size_;
            } else {
                pnl = (position_price_ - exit_price) * position_size_;
            }
            
            realized_pnl_ += pnl;
            ++trade_count_;
            if (pnl > 0) ++win_count_;
            
            position_side_ = CryptoSide::NONE;
            position_size_ = 0.0;
            position_price_ = 0.0;
            
            start_cooldown();
        }
    }
    
    // -------------------------------------------------------------------------
    // Order rejected/cancelled notification  
    // -------------------------------------------------------------------------
    void on_reject() noexcept {
        if (state_ == CryptoState::QUOTING || state_ == CryptoState::PENDING_FILL) {
            quote_side_ = CryptoSide::NONE;
            quote_price_ = 0.0;
            transition_to(CryptoState::IDLE);
        } else if (state_ == CryptoState::EXITING) {
            transition_to(CryptoState::IN_POSITION);
        }
    }
    
    // -------------------------------------------------------------------------
    // External controls
    // -------------------------------------------------------------------------
    void start() noexcept {
        if (state_ == CryptoState::HALTED || state_ == CryptoState::ERROR) {
            latency_sanity_.reset();
            spoofing_.reset();
            stability_.reset();
            transition_to(CryptoState::IDLE);
        }
    }
    
    void stop() noexcept {
        if (position_size_ > 0) {
            transition_to(CryptoState::EXITING);
        } else {
            transition_to(CryptoState::HALTED);
        }
    }
    
    void halt() noexcept {
        transition_to(CryptoState::HALTED);
    }
    
    void set_order_callback(OrderCallback cb) noexcept {
        order_callback_ = std::move(cb);
    }
    
    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------
    const std::string& symbol() const noexcept { return symbol_; }
    CryptoState state() const noexcept { return state_; }
    CryptoSide position_side() const noexcept { return position_side_; }
    double position_size() const noexcept { return position_size_; }
    double position_price() const noexcept { return position_price_; }
    double realized_pnl() const noexcept { return realized_pnl_; }
    double unrealized_pnl() const noexcept { return unrealized_pnl_; }
    uint64_t trade_count() const noexcept { return trade_count_; }
    double win_rate() const noexcept { 
        return trade_count_ > 0 ? 
               static_cast<double>(win_count_) / trade_count_ : 0.0; 
    }
    
    bool is_active() const noexcept {
        return state_ != CryptoState::HALTED && state_ != CryptoState::ERROR;
    }
    
    const CryptoTick& last_tick() const noexcept { return last_tick_; }
    const CryptoParams& params() const noexcept { return cfg_.params; }
    
    double stability_score() const noexcept { return stability_.score(); }
    double spoofing_score() const noexcept { return spoofing_.score(); }
    double latency_ema() const noexcept { return latency_sizer_.latency_ema(); }
    double queue_position() const noexcept { return queue_estimator_.queue_position(); }

private:
    // -------------------------------------------------------------------------
    // State handlers
    // -------------------------------------------------------------------------
    void handle_idle(const CryptoTick& tick) noexcept {
        if (!can_trade(tick)) return;
        
        CryptoSide signal = compute_signal(tick);
        if (signal == CryptoSide::NONE) return;
        
        double size = probe_sizer_.compute_size(
            stability_.volatility(),
            tick.spread_bps(),
            tick.bid_depth_5 + tick.ask_depth_5,
            stability_.score()
        );
        
        size *= latency_sizer_.multiplier();
        size = std::min(size, cfg_.params.base_size);
        
        double price = (signal == CryptoSide::BID) ? tick.bid_px : tick.ask_px;
        
        send_order(signal, price, size);
        quote_side_ = signal;
        quote_price_ = price;
        last_quote_ts_ = tick.local_ts;
        requote_count_ = 0;
        
        transition_to(CryptoState::QUOTING);
    }
    
    void handle_quoting(const CryptoTick& tick) noexcept {
        uint64_t age_ms = (tick.local_ts - last_quote_ts_) / 1000;
        
        if (age_ms > cfg_.params.quote_lifetime_ms) {
            cancel_order();
            transition_to(CryptoState::IDLE);
            return;
        }
        
        double current_price = (quote_side_ == CryptoSide::BID) ? 
                               tick.bid_px : tick.ask_px;
        
        if (std::abs(current_price - quote_price_) > quote_price_ * 0.0001) {
            if (requote_count_ < cfg_.params.max_requotes) {
                cancel_order();
                send_order(quote_side_, current_price, position_size_);
                quote_price_ = current_price;
                last_quote_ts_ = tick.local_ts;
                ++requote_count_;
            } else {
                cancel_order();
                transition_to(CryptoState::IDLE);
            }
        }
        
        queue_estimator_.set_order(
            quote_price_, 
            position_size_,
            (quote_side_ == CryptoSide::BID) ? tick.bid_depth_5 : tick.ask_depth_5,
            quote_side_
        );
    }
    
    void handle_pending_fill(const CryptoTick& tick) noexcept {
        uint64_t age_ms = (tick.local_ts - last_quote_ts_) / 1000;
        if (age_ms > cfg_.params.quote_lifetime_ms * 2) {
            cancel_order();
            transition_to(CryptoState::IDLE);
        }
    }
    
    void handle_in_position(const CryptoTick& tick) noexcept {
        double exit_price = (position_side_ == CryptoSide::BID) ? 
                            tick.ask_px : tick.bid_px;
        
        double pnl_bps = 0.0;
        if (position_side_ == CryptoSide::BID) {
            pnl_bps = ((exit_price - position_price_) / position_price_) * 10000.0;
        } else {
            pnl_bps = ((position_price_ - exit_price) / position_price_) * 10000.0;
        }
        
        unrealized_pnl_ = pnl_bps * position_size_ * position_price_ / 10000.0;
        
        if (pnl_bps >= cfg_.params.take_profit_bps) {
            initiate_exit(tick);
            return;
        }
        
        if (pnl_bps <= -cfg_.params.stop_loss_bps) {
            initiate_exit(tick);
            return;
        }
        
        if (spoofing_.is_spoofing_detected() || !stability_.is_stable()) {
            initiate_exit(tick);
        }
    }
    
    void handle_exiting(const CryptoTick& tick) noexcept {
        // Wait for fill callback
    }
    
    void handle_cooldown(const CryptoTick& tick) noexcept {
        if (tick.local_ts >= cooldown_until_) {
            transition_to(CryptoState::IDLE);
        }
    }
    
    // -------------------------------------------------------------------------
    // Trading logic helpers
    // -------------------------------------------------------------------------
    bool can_trade(const CryptoTick& tick) const noexcept {
        if (!tick.is_valid()) return false;
        if (tick.spread_bps() < cfg_.params.min_spread_bps) return false;
        if ((tick.bid_depth_5 + tick.ask_depth_5) < cfg_.params.min_depth_usd) return false;
        if (tick.toxic_flow > cfg_.params.max_toxic_flow) return false;
        if (tick.vpin > cfg_.params.max_vpin) return false;
        if (!stability_.is_stable()) return false;
        if (spoofing_.is_spoofing_detected()) return false;
        if (!latency_sizer_.is_latency_ok()) return false;
        return true;
    }
    
    CryptoSide compute_signal(const CryptoTick& tick) const noexcept {
        double imbalance = tick.imbalance;
        
        if (imbalance > 0.2 && tick.bid_depth_5 > tick.ask_depth_5 * 1.5) {
            return CryptoSide::BID;
        }
        
        if (imbalance < -0.2 && tick.ask_depth_5 > tick.bid_depth_5 * 1.5) {
            return CryptoSide::ASK;
        }
        
        return CryptoSide::NONE;
    }
    
    void initiate_exit(const CryptoTick& tick) noexcept {
        CryptoSide exit_side = (position_side_ == CryptoSide::BID) ? 
                               CryptoSide::ASK : CryptoSide::BID;
        double exit_price = (exit_side == CryptoSide::BID) ? tick.bid_px : tick.ask_px;
        
        send_order(exit_side, exit_price, position_size_, true);
        transition_to(CryptoState::EXITING);
    }
    
    void start_cooldown() noexcept {
        cooldown_until_ = last_tick_.local_ts + cfg_.params.cooldown_ms * 1000;
        transition_to(CryptoState::COOLDOWN);
    }
    
    // -------------------------------------------------------------------------
    // Order management
    // -------------------------------------------------------------------------
    void send_order(CryptoSide side, double price, double size, 
                    bool is_exit = false) noexcept {
        if (order_callback_) {
            order_callback_(symbol_, side, price, size, false);
        }
    }
    
    void cancel_order() noexcept {
        if (order_callback_ && quote_side_ != CryptoSide::NONE) {
            order_callback_(symbol_, quote_side_, quote_price_, 0.0, true);
        }
    }
    
    // -------------------------------------------------------------------------
    // State transition
    // -------------------------------------------------------------------------
    void transition_to(CryptoState new_state) noexcept {
        state_ = new_state;
    }
    
    // -------------------------------------------------------------------------
    // Member variables
    // -------------------------------------------------------------------------
    std::string symbol_;
    Config cfg_;
    
    SpoofingDetector spoofing_;
    AdaptiveStability stability_;
    LatencyAdaptiveSizer latency_sizer_;
    LatencySanity latency_sanity_;
    MakerQueueEstimator queue_estimator_;
    AdaptiveProbeSizer probe_sizer_;
    
    CryptoState state_;
    CryptoSide position_side_;
    double position_size_;
    double position_price_;
    
    CryptoSide quote_side_;
    double quote_price_;
    
    double realized_pnl_;
    double unrealized_pnl_;
    uint64_t trade_count_;
    uint64_t win_count_;
    
    uint64_t cooldown_until_;
    uint64_t last_quote_ts_;
    uint32_t requote_count_;
    
    CryptoTick last_tick_;
    OrderCallback order_callback_;
};

// =============================================================================
// CryptoScalpManager - Multi-symbol orchestrator
// =============================================================================

class CryptoScalpManager {
public:
    struct Config {
        CrossSymbolRiskGuard::Config risk_guard;
        CryptoScalpEngine::Config default_engine;
        uint32_t max_symbols;
        bool enable_portfolio_hedging;
        
        Config()
            : risk_guard()
            , default_engine()
            , max_symbols(10)
            , enable_portfolio_hedging(false)
        {}
    };
    
    explicit CryptoScalpManager(const Config& cfg = Config())
        : cfg_(cfg)
        , risk_guard_(cfg.risk_guard)
        , is_running_(false)
    {}
    
    // -------------------------------------------------------------------------
    // Symbol management
    // -------------------------------------------------------------------------
    bool add_symbol(const std::string& symbol, 
                    const CryptoScalpEngine::Config& engine_cfg) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (engines_.size() >= cfg_.max_symbols) return false;
        if (engines_.count(symbol) > 0) return false;
        
        auto engine = std::make_unique<CryptoScalpEngine>(symbol, engine_cfg);
        
        engine->set_order_callback(
            [this, symbol](const std::string& sym, CryptoSide side, 
                          double price, double size, bool is_cancel) {
                this->on_engine_order(sym, side, price, size, is_cancel);
            }
        );
        
        engines_[symbol] = std::move(engine);
        return true;
    }
    
    bool add_symbol(const std::string& symbol) {
        return add_symbol(symbol, cfg_.default_engine);
    }
    
    bool remove_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = engines_.find(symbol);
        if (it == engines_.end()) return false;
        
        it->second->stop();
        risk_guard_.remove_position(symbol);
        engines_.erase(it);
        return true;
    }
    
    // -------------------------------------------------------------------------
    // Tick routing
    // -------------------------------------------------------------------------
    void on_tick(const std::string& symbol, const CryptoTick& tick) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = engines_.find(symbol);
        if (it != engines_.end() && is_running_) {
            it->second->on_tick(tick);
        }
    }
    
    void on_fill(const std::string& symbol, CryptoSide side, 
                 double price, double size) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = engines_.find(symbol);
        if (it != engines_.end()) {
            it->second->on_fill(side, price, size);
            
            double exposure = price * size;
            if (side != CryptoSide::NONE) {
                risk_guard_.add_position(symbol, exposure);
            }
        }
    }
    
    void on_reject(const std::string& symbol) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = engines_.find(symbol);
        if (it != engines_.end()) {
            it->second->on_reject();
        }
    }
    
    // -------------------------------------------------------------------------
    // Global controls
    // -------------------------------------------------------------------------
    void start() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        is_running_ = true;
        for (auto& [sym, engine] : engines_) {
            engine->start();
        }
    }
    
    void stop() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        is_running_ = false;
        for (auto& [sym, engine] : engines_) {
            engine->stop();
        }
    }
    
    void halt() noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        is_running_ = false;
        for (auto& [sym, engine] : engines_) {
            engine->halt();
        }
    }
    
    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------
    CryptoScalpEngine* engine(const std::string& symbol) noexcept {
        auto it = engines_.find(symbol);
        return (it != engines_.end()) ? it->second.get() : nullptr;
    }
    
    const CryptoScalpEngine* engine(const std::string& symbol) const noexcept {
        auto it = engines_.find(symbol);
        return (it != engines_.end()) ? it->second.get() : nullptr;
    }
    
    size_t symbol_count() const noexcept { return engines_.size(); }
    bool is_running() const noexcept { return is_running_; }
    
    double total_realized_pnl() const noexcept {
        double total = 0.0;
        for (const auto& [sym, engine] : engines_) {
            total += engine->realized_pnl();
        }
        return total;
    }
    
    double total_unrealized_pnl() const noexcept {
        double total = 0.0;
        for (const auto& [sym, engine] : engines_) {
            total += engine->unrealized_pnl();
        }
        return total;
    }
    
    uint64_t total_trades() const noexcept {
        uint64_t total = 0;
        for (const auto& [sym, engine] : engines_) {
            total += engine->trade_count();
        }
        return total;
    }
    
    const CrossSymbolRiskGuard& risk_guard() const noexcept { return risk_guard_; }
    CrossSymbolRiskGuard& risk_guard() noexcept { return risk_guard_; }
    
    std::vector<std::string> symbols() const {
        std::vector<std::string> result;
        result.reserve(engines_.size());
        for (const auto& [sym, _] : engines_) {
            result.push_back(sym);
        }
        return result;
    }
    
    using OrderCallback = CryptoScalpEngine::OrderCallback;
    
    void set_order_callback(OrderCallback cb) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        global_order_callback_ = std::move(cb);
    }

private:
    void on_engine_order(const std::string& symbol, CryptoSide side,
                        double price, double size, bool is_cancel) {
        if (global_order_callback_) {
            global_order_callback_(symbol, side, price, size, is_cancel);
        }
    }
    
    Config cfg_;
    CrossSymbolRiskGuard risk_guard_;
    std::unordered_map<std::string, std::unique_ptr<CryptoScalpEngine>> engines_;
    std::mutex mtx_;
    bool is_running_;
    OrderCallback global_order_callback_;
};

} // namespace crypto
} // namespace chimera
