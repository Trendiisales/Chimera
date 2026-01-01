// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/shadow/ShadowExecutor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Shadow trading execution - simulates fills without sending orders
// OWNER: Jo
// VERSION: v3.0
//
// DESIGN:
// - Consumes live Binance market data
// - Runs full scalper logic (signals, gating, sizing)
// - Simulates fills using real bid/ask & spread
// - Logs PnL, expectancy, slippage
// - ZERO orders sent to Binance
//
// EXECUTION MODES:
// - TAKER_ONLY: Fill at best bid/ask immediately
// - MAKER_ONLY: Queue-aware probabilistic fills
// - HYBRID: Try maker first, fallback to taker
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <deque>
#include <string>
#include <fstream>
#include <chrono>
#include <random>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <mutex>

namespace Chimera {
namespace Shadow {

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time safety - CANNOT be bypassed
// ─────────────────────────────────────────────────────────────────────────────
#define SHADOW_MODE_ENABLED 1

#if !SHADOW_MODE_ENABLED
#error "Shadow mode must be enabled for this build"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode
// ─────────────────────────────────────────────────────────────────────────────
enum class ExecMode : uint8_t {
    TAKER_ONLY,      // Always take liquidity
    MAKER_ONLY,      // Always post passive
    HYBRID_SHADOW    // Try maker, fallback to taker
};

inline const char* exec_mode_str(ExecMode m) noexcept {
    switch (m) {
        case ExecMode::TAKER_ONLY:   return "TAKER_ONLY";
        case ExecMode::MAKER_ONLY:   return "MAKER_ONLY";
        case ExecMode::HYBRID_SHADOW: return "HYBRID";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Side
// ─────────────────────────────────────────────────────────────────────────────
enum class Side : uint8_t { BUY, SELL };

inline const char* side_str(Side s) noexcept {
    return s == Side::BUY ? "BUY" : "SELL";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fill Type
// ─────────────────────────────────────────────────────────────────────────────
enum class FillType : uint8_t {
    MAKER,           // Passive fill
    TAKER,           // Aggressive fill
    HYBRID_MAKER,    // Maker in hybrid mode
    HYBRID_TAKER,    // Taker fallback in hybrid
    NO_FILL,         // Order not filled (maker timeout/adverse)
    PARTIAL          // Partial fill
};

inline const char* fill_type_str(FillType f) noexcept {
    switch (f) {
        case FillType::MAKER:        return "maker";
        case FillType::TAKER:        return "taker";
        case FillType::HYBRID_MAKER: return "hybrid_maker";
        case FillType::HYBRID_TAKER: return "hybrid_taker";
        case FillType::NO_FILL:      return "no_fill";
        case FillType::PARTIAL:      return "partial";
        default: return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Fill Result
// ─────────────────────────────────────────────────────────────────────────────
struct ShadowFill {
    uint64_t timestamp_us;
    uint16_t symbol_id;
    std::string symbol;
    Side side;
    ExecMode mode;
    FillType fill_type;
    double qty;
    double entry_price;
    double exit_price;
    double spread_bps;
    double slippage_bps;
    uint64_t hold_time_ms;
    double pnl_usdt;
    double pnl_bps;
    double expectancy_at_entry;
    std::string reason;
    
    bool filled = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Market Snapshot (required for simulation)
// ─────────────────────────────────────────────────────────────────────────────
struct MarketSnapshot {
    double best_bid = 0.0;
    double best_ask = 0.0;
    double bid_qty = 0.0;
    double ask_qty = 0.0;
    double mid_price = 0.0;
    double spread_bps = 0.0;
    double recent_taker_volume = 0.0;  // For maker queue simulation
    uint64_t timestamp_us = 0;
    
    [[nodiscard]] bool valid() const noexcept {
        return best_bid > 0 && best_ask > 0 && best_ask > best_bid;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Maker Health Tracker (per symbol)
// ─────────────────────────────────────────────────────────────────────────────
struct MakerHealth {
    double fill_rate = 0.5;        // EMA of fill success
    double adverse_rate = 0.0;     // EMA of adverse selection aborts
    double expectancy_bps = 0.0;   // EMA of maker expectancy
    int total_attempts = 0;
    int total_fills = 0;
    int total_adverse = 0;
    bool enabled = true;
    uint64_t cooldown_until_us = 0;
    
    static constexpr double ALPHA = 0.1;
    static constexpr double MIN_FILL_RATE = 0.20;
    static constexpr double MAX_ADVERSE_RATE = 0.30;
    static constexpr uint64_t COOLDOWN_US = 15 * 60 * 1000000ULL;  // 15 min
    
    void record_fill(double pnl_bps) noexcept {
        total_attempts++;
        total_fills++;
        fill_rate = ALPHA * 1.0 + (1.0 - ALPHA) * fill_rate;
        expectancy_bps = ALPHA * pnl_bps + (1.0 - ALPHA) * expectancy_bps;
        check_health();
    }
    
    void record_no_fill() noexcept {
        total_attempts++;
        fill_rate = ALPHA * 0.0 + (1.0 - ALPHA) * fill_rate;
        check_health();
    }
    
    void record_adverse() noexcept {
        total_attempts++;
        total_adverse++;
        adverse_rate = ALPHA * 1.0 + (1.0 - ALPHA) * adverse_rate;
        check_health();
    }
    
    void check_health() noexcept {
        if (fill_rate < MIN_FILL_RATE || 
            adverse_rate > MAX_ADVERSE_RATE || 
            expectancy_bps < 0.0) {
            enabled = false;
            cooldown_until_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + COOLDOWN_US;
        }
    }
    
    [[nodiscard]] bool can_use_maker(uint64_t now_us) const noexcept {
        if (!enabled && now_us < cooldown_until_us) return false;
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Expectancy Slope Tracker (per symbol)
// ─────────────────────────────────────────────────────────────────────────────
struct ExpectancySlopeTracker {
    std::deque<double> expectancy_history;
    bool paused = false;
    
    static constexpr int WINDOW = 50;
    static constexpr double NEG_SLOPE_THRESH = -0.005;  // bps/trade
    static constexpr double POS_SLOPE_RECOVER = +0.002;
    static constexpr int MIN_TRADES = 10;
    
    void record(double expectancy_bps) noexcept {
        expectancy_history.push_back(expectancy_bps);
        if (expectancy_history.size() > WINDOW) {
            expectancy_history.pop_front();
        }
        check_pause();
    }
    
    [[nodiscard]] double compute_slope() const noexcept {
        int n = static_cast<int>(expectancy_history.size());
        if (n < MIN_TRADES) return 0.0;
        
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        for (int i = 0; i < n; i++) {
            sum_x += i;
            sum_y += expectancy_history[i];
            sum_xy += i * expectancy_history[i];
            sum_x2 += i * i;
        }
        
        double denom = n * sum_x2 - sum_x * sum_x;
        if (denom == 0) return 0.0;
        
        return (n * sum_xy - sum_x * sum_y) / denom;
    }
    
    void check_pause() noexcept {
        double slope = compute_slope();
        int n = static_cast<int>(expectancy_history.size());
        
        if (!paused) {
            if (slope < NEG_SLOPE_THRESH && n >= MIN_TRADES) {
                paused = true;
                std::cout << "[SHADOW] Symbol PAUSED - expectancy slope: " << slope << " bps/trade\n";
            }
        } else {
            // Check for recovery
            if (slope > POS_SLOPE_RECOVER && !expectancy_history.empty() && expectancy_history.back() > 0) {
                paused = false;
                std::cout << "[SHADOW] Symbol RESUMED - expectancy slope recovered: " << slope << " bps/trade\n";
            }
        }
    }
    
    [[nodiscard]] bool is_paused() const noexcept { return paused; }
    [[nodiscard]] double get_slope() const noexcept { return compute_slope(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Position Tracker (for shadow PnL)
// ─────────────────────────────────────────────────────────────────────────────
struct ShadowPosition {
    bool has_position = false;
    Side side = Side::BUY;
    double qty = 0.0;
    double entry_price = 0.0;
    uint64_t entry_time_us = 0;
    double entry_expectancy = 0.0;
    FillType entry_fill_type = FillType::TAKER;
};

// ─────────────────────────────────────────────────────────────────════════════
// Shadow Executor - Main Class
// ═══════════════════════════════════════════════════════════════════════════════
class ShadowExecutor {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    struct Config {
        ExecMode mode = ExecMode::TAKER_ONLY;
        double fixed_qty = 0.00008;        // BTC - fixed for testing
        double min_qty = 0.00006;
        double max_qty = 0.00008;
        uint64_t maker_timeout_ms = 300;
        double min_queue_fill_prob = 0.25;
        double adverse_move_bps = 0.3;
        double min_spread_for_maker = 0.8; // bps
        std::string csv_path = "shadow_trades.csv";
        bool log_to_csv = true;
        bool sound_on_fill = false;
    };
    
    explicit ShadowExecutor(const Config& config = Config{})
        : config_(config)
        , rng_(std::random_device{}())
    {
        // Initialize CSV
        if (config_.log_to_csv) {
            init_csv();
        }
        
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  SHADOW EXECUTOR v3.0 INITIALIZED                            ║\n";
        std::cout << "║  Mode: " << std::setw(15) << std::left << exec_mode_str(config_.mode) << "                                  ║\n";
        std::cout << "║  Fixed Qty: " << config_.fixed_qty << " BTC                              ║\n";
        std::cout << "║  CSV: " << config_.csv_path << "                               ║\n";
        std::cout << "║  ⚠️  NO REAL ORDERS WILL BE SENT                              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }
    
    ~ShadowExecutor() {
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
        print_summary();
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Execute Shadow Trade (main entry point)
    // ─────────────────────────────────────────────────────────────────────────
    ShadowFill execute(
        uint16_t symbol_id,
        const std::string& symbol,
        Side side,
        const MarketSnapshot& market,
        double current_expectancy,
        const std::string& reason
    ) noexcept {
        ShadowFill fill{};
        fill.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        fill.symbol_id = symbol_id;
        fill.symbol = symbol;
        fill.side = side;
        fill.mode = config_.mode;
        fill.qty = config_.fixed_qty;
        fill.expectancy_at_entry = current_expectancy;
        fill.reason = reason;
        fill.spread_bps = market.spread_bps;
        
        // Check if symbol is paused
        auto& slope_tracker = get_slope_tracker(symbol_id);
        if (slope_tracker.is_paused()) {
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "EXPECTANCY_SLOPE_PAUSED";
            return fill;
        }
        
        // Check market validity
        if (!market.valid()) {
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "INVALID_MARKET";
            return fill;
        }
        
        // Execute based on mode
        switch (config_.mode) {
            case ExecMode::TAKER_ONLY:
                fill = simulate_taker(fill, market);
                break;
            case ExecMode::MAKER_ONLY:
                fill = simulate_maker(fill, market);
                break;
            case ExecMode::HYBRID_SHADOW:
                fill = simulate_hybrid(fill, market);
                break;
        }
        
        // If we got an entry, store position
        if (fill.filled) {
            open_position(symbol_id, fill);
            total_trades_++;
            
            // Log to console
            log_trade(fill);
            
            // Log to CSV
            if (config_.log_to_csv) {
                log_csv(fill);
            }
        }
        
        return fill;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Close Position (exit trade)
    // ─────────────────────────────────────────────────────────────────────────
    ShadowFill close_position(
        uint16_t symbol_id,
        const std::string& symbol,
        const MarketSnapshot& market,
        const std::string& reason
    ) noexcept {
        ShadowFill fill{};
        fill.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        fill.symbol_id = symbol_id;
        fill.symbol = symbol;
        fill.reason = reason;
        fill.spread_bps = market.spread_bps;
        fill.mode = config_.mode;
        
        auto& pos = get_position(symbol_id);
        if (!pos.has_position) {
            fill.filled = false;
            fill.fill_type = FillType::NO_FILL;
            return fill;
        }
        
        // Exit is always taker (get out immediately)
        fill.side = (pos.side == Side::BUY) ? Side::SELL : Side::BUY;
        fill.qty = pos.qty;
        fill.entry_price = pos.entry_price;
        fill.fill_type = FillType::TAKER;
        fill.filled = true;
        
        // Calculate exit price
        if (pos.side == Side::BUY) {
            fill.exit_price = market.best_bid;  // Sell at bid
        } else {
            fill.exit_price = market.best_ask;  // Buy at ask
        }
        
        // Calculate PnL
        if (pos.side == Side::BUY) {
            fill.pnl_usdt = (fill.exit_price - fill.entry_price) * fill.qty;
        } else {
            fill.pnl_usdt = (fill.entry_price - fill.exit_price) * fill.qty;
        }
        
        double notional = fill.entry_price * fill.qty;
        fill.pnl_bps = (fill.pnl_usdt / notional) * 10000.0;
        fill.slippage_bps = std::abs(fill.exit_price - market.mid_price) / market.mid_price * 10000.0;
        fill.hold_time_ms = (fill.timestamp_us - pos.entry_time_us) / 1000;
        fill.expectancy_at_entry = pos.entry_expectancy;
        
        // Update stats
        total_pnl_usdt_ += fill.pnl_usdt;
        total_pnl_bps_ += fill.pnl_bps;
        if (fill.pnl_usdt > 0) {
            wins_++;
            total_win_amount_ += fill.pnl_usdt;
        } else {
            losses_++;
            total_loss_amount_ += std::abs(fill.pnl_usdt);
        }
        
        // Update expectancy tracker
        auto& slope_tracker = get_slope_tracker(symbol_id);
        slope_tracker.record(fill.pnl_bps);
        
        // Clear position
        pos.has_position = false;
        
        // Log
        log_trade(fill);
        if (config_.log_to_csv) {
            log_csv(fill);
        }
        
        return fill;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Check
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool has_position(uint16_t symbol_id) const noexcept {
        auto it = positions_.find(symbol_id);
        if (it == positions_.end()) return false;
        return it->second.has_position;
    }
    
    [[nodiscard]] const ShadowPosition* get_position_ptr(uint16_t symbol_id) const noexcept {
        auto it = positions_.find(symbol_id);
        if (it == positions_.end()) return nullptr;
        return &it->second;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Statistics
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] uint64_t total_trades() const noexcept { return total_trades_; }
    [[nodiscard]] uint64_t wins() const noexcept { return wins_; }
    [[nodiscard]] uint64_t losses() const noexcept { return losses_; }
    [[nodiscard]] double total_pnl_usdt() const noexcept { return total_pnl_usdt_; }
    [[nodiscard]] double total_pnl_bps() const noexcept { return total_pnl_bps_; }
    [[nodiscard]] double win_rate() const noexcept {
        if (wins_ + losses_ == 0) return 0.0;
        return static_cast<double>(wins_) / (wins_ + losses_) * 100.0;
    }
    [[nodiscard]] double avg_expectancy() const noexcept {
        if (total_trades_ == 0) return 0.0;
        return total_pnl_bps_ / total_trades_;
    }
    
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║              SHADOW TRADING SUMMARY                          ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  Total Trades: " << std::setw(10) << total_trades_ << "                                  ║\n";
        std::cout << "║  Wins:         " << std::setw(10) << wins_ << "  (" << std::fixed << std::setprecision(1) << win_rate() << "%)                       ║\n";
        std::cout << "║  Losses:       " << std::setw(10) << losses_ << "                                  ║\n";
        std::cout << "║  Total PnL:    $" << std::setw(9) << std::fixed << std::setprecision(2) << total_pnl_usdt_ << "                                  ║\n";
        std::cout << "║  Avg Expect:   " << std::setw(8) << std::setprecision(2) << avg_expectancy() << " bps                             ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Taker Simulation (immediate fill at best bid/ask)
    // ─────────────────────────────────────────────────────────────────────────
    ShadowFill simulate_taker(ShadowFill fill, const MarketSnapshot& market) noexcept {
        fill.fill_type = FillType::TAKER;
        fill.filled = true;
        
        // Entry: Buy at ask, Sell at bid
        if (fill.side == Side::BUY) {
            fill.entry_price = market.best_ask;
        } else {
            fill.entry_price = market.best_bid;
        }
        
        // Calculate slippage vs mid
        fill.slippage_bps = std::abs(fill.entry_price - market.mid_price) / market.mid_price * 10000.0;
        
        return fill;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Maker Simulation (queue-aware probabilistic fill)
    // ─────────────────────────────────────────────────────────────────────────
    ShadowFill simulate_maker(ShadowFill fill, const MarketSnapshot& market) noexcept {
        uint64_t now_us = fill.timestamp_us;
        
        // Check maker health
        auto& maker_health = get_maker_health(fill.symbol_id);
        if (!maker_health.can_use_maker(now_us)) {
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "MAKER_DISABLED";
            return fill;
        }
        
        // Check spread requirement
        if (market.spread_bps < config_.min_spread_for_maker) {
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "SPREAD_TOO_TIGHT";
            return fill;
        }
        
        // Estimate queue ahead
        double queue_ahead = (fill.side == Side::BUY) ? market.bid_qty : market.ask_qty;
        queue_ahead *= 1.5;  // Pessimistic factor
        
        // Calculate fill probability
        double fill_prob = 0.0;
        if (market.recent_taker_volume > 0 && queue_ahead > 0) {
            fill_prob = 1.0 - std::exp(-market.recent_taker_volume / queue_ahead);
        }
        
        // Check minimum probability
        if (fill_prob < config_.min_queue_fill_prob) {
            maker_health.record_no_fill();
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "LOW_FILL_PROB";
            return fill;
        }
        
        // Random fill decision
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double r = dist(rng_);
        
        if (r >= fill_prob) {
            maker_health.record_no_fill();
            fill.fill_type = FillType::NO_FILL;
            fill.filled = false;
            fill.reason = "QUEUE_NOT_REACHED";
            return fill;
        }
        
        // Filled as maker
        fill.fill_type = FillType::MAKER;
        fill.filled = true;
        
        // Maker fills at limit price (best bid for buy, best ask for sell)
        if (fill.side == Side::BUY) {
            fill.entry_price = market.best_bid;
        } else {
            fill.entry_price = market.best_ask;
        }
        
        // Maker slippage is near-zero (we capture spread)
        fill.slippage_bps = 0.0;
        
        maker_health.record_fill(0.0);  // PnL tracked on exit
        
        return fill;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Hybrid Simulation (maker first, taker fallback)
    // ─────────────────────────────────────────────────────────────────────────
    ShadowFill simulate_hybrid(ShadowFill fill, const MarketSnapshot& market) noexcept {
        uint64_t now_us = fill.timestamp_us;
        
        // Check if maker is allowed
        auto& maker_health = get_maker_health(fill.symbol_id);
        bool allow_maker = maker_health.can_use_maker(now_us) &&
                          market.spread_bps >= config_.min_spread_for_maker;
        
        if (!allow_maker) {
            // Go straight to taker
            fill = simulate_taker(fill, market);
            fill.fill_type = FillType::HYBRID_TAKER;
            return fill;
        }
        
        // Try maker first
        ShadowFill maker_attempt = simulate_maker(fill, market);
        
        if (maker_attempt.filled) {
            maker_attempt.fill_type = FillType::HYBRID_MAKER;
            return maker_attempt;
        }
        
        // Maker failed - fallback to taker
        fill = simulate_taker(fill, market);
        fill.fill_type = FillType::HYBRID_TAKER;
        
        return fill;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Management
    // ─────────────────────────────────────────────────────────────────────────
    void open_position(uint16_t symbol_id, const ShadowFill& fill) noexcept {
        auto& pos = get_position(symbol_id);
        pos.has_position = true;
        pos.side = fill.side;
        pos.qty = fill.qty;
        pos.entry_price = fill.entry_price;
        pos.entry_time_us = fill.timestamp_us;
        pos.entry_expectancy = fill.expectancy_at_entry;
        pos.entry_fill_type = fill.fill_type;
    }
    
    ShadowPosition& get_position(uint16_t symbol_id) noexcept {
        return positions_[symbol_id];
    }
    
    MakerHealth& get_maker_health(uint16_t symbol_id) noexcept {
        return maker_health_[symbol_id];
    }
    
    ExpectancySlopeTracker& get_slope_tracker(uint16_t symbol_id) noexcept {
        return slope_trackers_[symbol_id];
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Logging
    // ─────────────────────────────────────────────────────────────────────────
    void init_csv() {
        csv_file_.open(config_.csv_path, std::ios::out | std::ios::trunc);
        if (csv_file_.is_open()) {
            csv_file_ << "timestamp,symbol,mode,side,qty,entry,exit,fill_type,spread_bps,"
                     << "slippage_bps,hold_ms,pnl_usdt,pnl_bps,expectancy,reason\n";
            csv_file_.flush();
        }
    }
    
    void log_csv(const ShadowFill& fill) {
        if (!csv_file_.is_open()) return;
        
        csv_file_ << fill.timestamp_us << ","
                 << fill.symbol << ","
                 << exec_mode_str(fill.mode) << ","
                 << side_str(fill.side) << ","
                 << std::fixed << std::setprecision(8) << fill.qty << ","
                 << std::setprecision(2) << fill.entry_price << ","
                 << fill.exit_price << ","
                 << fill_type_str(fill.fill_type) << ","
                 << std::setprecision(4) << fill.spread_bps << ","
                 << fill.slippage_bps << ","
                 << fill.hold_time_ms << ","
                 << std::setprecision(6) << fill.pnl_usdt << ","
                 << std::setprecision(4) << fill.pnl_bps << ","
                 << fill.expectancy_at_entry << ","
                 << fill.reason << "\n";
        csv_file_.flush();
    }
    
    void log_trade(const ShadowFill& fill) const {
        const char* color = fill.pnl_usdt >= 0 ? "\033[32m" : "\033[31m";
        const char* reset = "\033[0m";
        
        std::cout << "[SHADOW] " << fill.symbol << " "
                 << side_str(fill.side) << " "
                 << fill.qty << " @ "
                 << std::fixed << std::setprecision(2) << fill.entry_price;
        
        if (fill.exit_price > 0) {
            std::cout << " → " << fill.exit_price
                     << " | PnL: " << color << "$" << std::setprecision(4) << fill.pnl_usdt
                     << " (" << fill.pnl_bps << " bps)" << reset
                     << " | " << fill.hold_time_ms << "ms"
                     << " | " << fill_type_str(fill.fill_type);
        }
        
        std::cout << " | " << fill.reason << "\n";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────────
    Config config_;
    std::mt19937 rng_;
    std::ofstream csv_file_;
    
    std::unordered_map<uint16_t, ShadowPosition> positions_;
    std::unordered_map<uint16_t, MakerHealth> maker_health_;
    std::unordered_map<uint16_t, ExpectancySlopeTracker> slope_trackers_;
    
    // Stats
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<uint64_t> wins_{0};
    std::atomic<uint64_t> losses_{0};
    double total_pnl_usdt_ = 0.0;
    double total_pnl_bps_ = 0.0;
    double total_win_amount_ = 0.0;
    double total_loss_amount_ = 0.0;
};

} // namespace Shadow
} // namespace Chimera
