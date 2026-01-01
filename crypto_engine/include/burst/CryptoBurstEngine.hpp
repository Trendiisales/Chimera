// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/burst/CryptoBurstEngine.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED DESIGN
// VERSION: v1.0.0
// OWNER: Jo
// 
// ═══════════════════════════════════════════════════════════════════════════════
// OPERATING CONTRACT (READ THIS BEFORE TOUCHING ANYTHING):
// ═══════════════════════════════════════════════════════════════════════════════
// 
// 1. Crypto is OFF by default. It turns ON only when ALL pre-gate conditions
//    are SIMULTANEOUSLY met. There is no "tuning to see more trades."
// 
// 2. SUCCESS METRIC: RARE, high-expectancy wins; ZERO bleed otherwise.
//    If crypto trades frequently → something is WRONG.
// 
// 3. EXPECTED BEHAVIOR:
//    - Days with 0 trades: NORMAL and CORRECT
//    - 1-3 trades per week: OPTIMAL
//    - >5 trades per week: INVESTIGATE - gate likely compromised
// 
// 4. SILENCE IS INTENTIONAL. When idle, the engine logs WHY it's idle.
//    "No trade" = system protecting capital, NOT failing.
// 
// 5. NEVER:
//    - Relax pre-gate conditions to "see action"
//    - Add symbols beyond the approved list
//    - Re-enter during cooldown
//    - Scale into positions
// 
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace chimera {
namespace crypto {
namespace burst {

// ═══════════════════════════════════════════════════════════════════════════════
// ENUMS & TYPE DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════════════

enum class BurstSymbol : uint8_t {
    BTCUSDT = 0,    // LIVE - primary
    ETHUSDT = 1,    // SHADOW only
    SOLUSDT = 2     // SHADOW only
};

enum class BurstMode : uint8_t {
    LIVE,           // Real execution
    SHADOW          // Paper/logging only
};

enum class Regime : uint8_t {
    UNKNOWN = 0,
    RANGING,
    TRENDING,
    TRANSITION
};

enum class Direction : uint8_t {
    NONE = 0,
    LONG,
    SHORT
};

enum class ExitReason : uint8_t {
    NONE = 0,
    TIME_STOP,
    STRUCTURE_BREAK,
    MAX_ADVERSE,
    MANUAL,
    TARGET_HIT,
    DAILY_LIMIT
};

enum class GateBlock : uint8_t {
    NONE = 0,               // Gate clear - allowed to trade
    VOL_EXPANSION_LOW,      // Volatility not expanded enough
    SPREAD_TOO_WIDE,        // Spread not compressed
    IMBALANCE_WEAK,         // Book imbalance insufficient
    DISPLACEMENT_LOW,       // Price displacement insufficient
    REGIME_NOT_TRENDING,    // Not in TRENDING regime
    COOLDOWN_ACTIVE,        // In cooldown period
    ALREADY_IN_POSITION,    // Already have position
    EDGE_LESS_THAN_COST,    // Edge < 3x cost
    SYMBOL_SHADOW_ONLY,     // Symbol not live-enabled
    DAILY_LOSS_LIMIT,       // Hit daily loss limit
    MAX_DAILY_TRADES        // Hit max trades per day
};

// ─────────────────────────────────────────────────────────────────────────────
// String conversions (cold path - logging only)
// ─────────────────────────────────────────────────────────────────────────────

inline const char* symbol_str(BurstSymbol s) noexcept {
    switch (s) {
        case BurstSymbol::BTCUSDT: return "BTCUSDT";
        case BurstSymbol::ETHUSDT: return "ETHUSDT";
        case BurstSymbol::SOLUSDT: return "SOLUSDT";
        default: return "UNKNOWN";
    }
}

inline const char* regime_str(Regime r) noexcept {
    switch (r) {
        case Regime::UNKNOWN:    return "UNKNOWN";
        case Regime::RANGING:    return "RANGING";
        case Regime::TRENDING:   return "TRENDING";
        case Regime::TRANSITION: return "TRANSITION";
        default: return "UNKNOWN";
    }
}

inline const char* direction_str(Direction d) noexcept {
    switch (d) {
        case Direction::NONE:  return "NONE";
        case Direction::LONG:  return "LONG";
        case Direction::SHORT: return "SHORT";
        default: return "UNKNOWN";
    }
}

inline const char* exit_str(ExitReason e) noexcept {
    switch (e) {
        case ExitReason::NONE:            return "NONE";
        case ExitReason::TIME_STOP:       return "TIME_STOP";
        case ExitReason::STRUCTURE_BREAK: return "STRUCTURE_BREAK";
        case ExitReason::MAX_ADVERSE:     return "MAX_ADVERSE";
        case ExitReason::MANUAL:          return "MANUAL";
        case ExitReason::TARGET_HIT:      return "TARGET_HIT";
        case ExitReason::DAILY_LIMIT:     return "DAILY_LIMIT";
        default: return "UNKNOWN";
    }
}

inline const char* block_str(GateBlock b) noexcept {
    switch (b) {
        case GateBlock::NONE:               return "CLEAR";
        case GateBlock::VOL_EXPANSION_LOW:  return "VOL_LOW";
        case GateBlock::SPREAD_TOO_WIDE:    return "SPREAD_WIDE";
        case GateBlock::IMBALANCE_WEAK:     return "IMBAL_WEAK";
        case GateBlock::DISPLACEMENT_LOW:   return "DISP_LOW";
        case GateBlock::REGIME_NOT_TRENDING:return "REGIME_BAD";
        case GateBlock::COOLDOWN_ACTIVE:    return "COOLDOWN";
        case GateBlock::ALREADY_IN_POSITION:return "IN_POS";
        case GateBlock::EDGE_LESS_THAN_COST:return "EDGE_LOW";
        case GateBlock::SYMBOL_SHADOW_ONLY: return "SHADOW";
        case GateBlock::DAILY_LOSS_LIMIT:   return "DAILY_LOSS";
        case GateBlock::MAX_DAILY_TRADES:   return "MAX_TRADES";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURATION STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Pre-gate thresholds - ALL must pass simultaneously
 */
struct BurstGateConfig {
    // Volatility expansion: realized vol >= 2.0x trailing median
    double vol_expansion_min = 2.0;
    
    // Spread compression: spread <= p25 of last 30 min (not widening)
    double spread_percentile_max = 25.0;
    
    // Book imbalance: top-N liquidity imbalance >= 65/35
    double imbalance_ratio_min = 0.65;
    
    // Displacement: price move >= N ticks (BTC: 6 ticks = ~$6)
    int displacement_ticks_min = 6;
    
    // Regime: must be TRENDING (no TRANSITION-only entries)
    Regime required_regime = Regime::TRENDING;
    
    // Edge requirement: edge >= 3x total cost
    double edge_to_cost_min = 3.0;
};

/**
 * Entry rules configuration
 */
struct BurstEntryConfig {
    bool single_entry_only = true;          // No scaling in - EVER
    bool taker_allowed = true;              // Fees irrelevant vs burst move
    double max_position_btc = 0.001;        // BTC units (conservative)
    int max_concurrent_positions = 1;       // Only 1 position at a time
};

/**
 * Exit rules configuration
 */
struct BurstExitConfig {
    int time_stop_min_sec = 5;              // Minimum hold time
    int time_stop_max_sec = 30;             // Maximum hold time before forced exit
    double max_adverse_r = 0.5;             // Max adverse excursion (tight)
    
    // Structure break detection
    bool structure_break_exit = true;
    double imbalance_collapse_threshold = 0.50; // 50/50 = structure broken
};

/**
 * Cooldown configuration - HARD, no exceptions, no overrides
 */
struct BurstCooldownConfig {
    int cooldown_after_win_sec = 300;       // 5 minutes after win
    int cooldown_after_loss_sec = 900;      // 15 minutes after loss
    int cooldown_after_no_fill_sec = 60;    // 1 minute after no fill
};

/**
 * Symbol-specific configuration
 */
struct BurstSymbolConfig {
    BurstSymbol symbol;
    BurstMode mode;                         // LIVE or SHADOW
    
    // Tick/price configuration
    double tick_size;                       // BTC: 0.01
    double min_displacement_usd;            // Min displacement in USD
    
    // Position sizing
    double base_size;                       // Base position size
    double max_size;                        // Max position size
    
    // Fees (Binance spot)
    double taker_fee_bps = 10.0;            // 0.10% taker
    double maker_fee_bps = 10.0;            // 0.10% maker
    
    // Factory for BTCUSDT (LIVE)
    static BurstSymbolConfig btcusdt_live() {
        return BurstSymbolConfig{
            .symbol = BurstSymbol::BTCUSDT,
            .mode = BurstMode::LIVE,
            .tick_size = 0.01,
            .min_displacement_usd = 60.0,   // ~6 ticks at $100k
            .base_size = 0.0005,
            .max_size = 0.001,
            .taker_fee_bps = 10.0,
            .maker_fee_bps = 10.0
        };
    }
    
    // Factory for ETHUSDT (SHADOW)
    static BurstSymbolConfig ethusdt_shadow() {
        return BurstSymbolConfig{
            .symbol = BurstSymbol::ETHUSDT,
            .mode = BurstMode::SHADOW,
            .tick_size = 0.01,
            .min_displacement_usd = 4.0,    // ~6 ticks at ETH price
            .base_size = 0.005,
            .max_size = 0.01,
            .taker_fee_bps = 10.0,
            .maker_fee_bps = 10.0
        };
    }
    
    // Factory for SOLUSDT (SHADOW)
    static BurstSymbolConfig solusdt_shadow() {
        return BurstSymbolConfig{
            .symbol = BurstSymbol::SOLUSDT,
            .mode = BurstMode::SHADOW,
            .tick_size = 0.001,
            .min_displacement_usd = 0.60,   // ~6 ticks at SOL price
            .base_size = 0.1,
            .max_size = 0.5,
            .taker_fee_bps = 10.0,
            .maker_fee_bps = 10.0
        };
    }
};

/**
 * Master engine configuration
 */
struct BurstEngineConfig {
    BurstGateConfig gate;
    BurstEntryConfig entry;
    BurstExitConfig exit;
    BurstCooldownConfig cooldown;
    
    std::vector<BurstSymbolConfig> symbols;
    
    // Logging
    bool log_idle_state = true;
    int idle_log_interval_sec = 60;         // Log idle state every 60s
    
    // Safety - daily limits
    double daily_loss_limit_usd = 100.0;    // Hard stop
    int max_daily_trades = 5;               // Circuit breaker
    
    // Factory for default config (BTC live only)
    static BurstEngineConfig btc_only() {
        BurstEngineConfig cfg;
        cfg.symbols.push_back(BurstSymbolConfig::btcusdt_live());
        cfg.symbols.push_back(BurstSymbolConfig::ethusdt_shadow());
        cfg.symbols.push_back(BurstSymbolConfig::solusdt_shadow());
        return cfg;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Order book level
 */
struct BookLevel {
    double price = 0.0;
    double qty = 0.0;
};

/**
 * Order book snapshot (from WebSocket)
 */
struct alignas(64) BurstBook {
    BurstSymbol symbol;
    uint64_t exchange_ts = 0;               // Exchange timestamp (ms)
    uint64_t local_ts = 0;                  // Local receive timestamp (us)
    
    std::array<BookLevel, 20> bids;         // Best bid first
    std::array<BookLevel, 20> asks;         // Best ask first
    uint8_t bid_levels = 0;
    uint8_t ask_levels = 0;
    
    double best_bid() const noexcept { 
        return bid_levels > 0 ? bids[0].price : 0.0; 
    }
    double best_ask() const noexcept { 
        return ask_levels > 0 ? asks[0].price : 0.0; 
    }
    double mid() const noexcept { 
        return (best_bid() + best_ask()) * 0.5; 
    }
    double spread() const noexcept { 
        return best_ask() - best_bid(); 
    }
    double spread_bps() const noexcept {
        double m = mid();
        return m > 0 ? (spread() / m) * 10000.0 : 0.0;
    }
    bool is_valid() const noexcept {
        return bid_levels > 0 && ask_levels > 0 && best_ask() > best_bid();
    }
};

/**
 * Aggregate trade (from aggTrade stream)
 */
struct BurstTrade {
    BurstSymbol symbol;
    double price = 0.0;
    double qty = 0.0;
    bool is_buyer_maker = false;            // true = sell, false = buy
    uint64_t exchange_ts = 0;
    uint64_t local_ts = 0;
};

/**
 * Gate status - shows exactly why we're blocked or ready
 */
struct GateStatus {
    // Individual checks
    bool vol_ok = false;
    bool spread_ok = false;
    bool imbalance_ok = false;
    bool displacement_ok = false;
    bool regime_ok = false;
    bool cooldown_ok = false;
    bool no_position_ok = false;
    bool edge_ok = false;
    bool daily_ok = false;
    bool max_trades_ok = false;
    
    // Primary block reason (first failed check)
    GateBlock primary_block = GateBlock::NONE;
    
    // Actual values for logging
    double vol_expansion = 0.0;
    double spread_percentile = 0.0;
    double imbalance_ratio = 0.0;
    int displacement_ticks = 0;
    Regime current_regime = Regime::UNKNOWN;
    double edge_to_cost = 0.0;
    int seconds_until_cooldown_end = 0;
    
    bool all_clear() const noexcept {
        return vol_ok && spread_ok && imbalance_ok && displacement_ok &&
               regime_ok && cooldown_ok && no_position_ok && edge_ok &&
               daily_ok && max_trades_ok;
    }
    
    std::string to_log_string() const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "[CRYPTO] %s — vol=%.2fx(%s) spread=p%.0f(%s) imbal=%.0f/%.0f(%s) "
            "disp=%dt(%s) regime=%s(%s) cd=%ds edge=%.1fx(%s)",
            all_clear() ? "ARMED" : "OFF",
            vol_expansion, vol_ok ? "OK" : "LOW",
            spread_percentile, spread_ok ? "OK" : "WIDE",
            imbalance_ratio * 100, (1.0 - imbalance_ratio) * 100, imbalance_ok ? "OK" : "WEAK",
            displacement_ticks, displacement_ok ? "OK" : "LOW",
            regime_str(current_regime), regime_ok ? "OK" : "BLOCKED",
            seconds_until_cooldown_end,
            edge_to_cost, edge_ok ? "OK" : "LOW"
        );
        return std::string(buf);
    }
};

/**
 * Position state
 */
struct BurstPosition {
    BurstSymbol symbol = BurstSymbol::BTCUSDT;
    Direction direction = Direction::NONE;
    double entry_price = 0.0;
    double size = 0.0;
    double current_price = 0.0;
    double unrealized_pnl = 0.0;
    double max_adverse_pnl = 0.0;           // Track worst drawdown
    uint64_t entry_ts = 0;                  // Entry timestamp (us)
    
    // Gate conditions at entry (for analysis)
    double vol_at_entry = 0.0;
    double imbalance_at_entry = 0.0;
    int displacement_at_entry = 0;
    
    bool is_open() const noexcept { 
        return direction != Direction::NONE && size > 0.0; 
    }
    
    double pnl_r(double risk_amount) const noexcept {
        return risk_amount > 0 ? unrealized_pnl / risk_amount : 0.0;
    }
    
    int hold_duration_ms() const noexcept {
        if (entry_ts == 0) return 0;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        return static_cast<int>((now_us - entry_ts) / 1000);
    }
};

/**
 * Trade result for logging/stats
 */
struct BurstTradeResult {
    BurstSymbol symbol;
    Direction direction;
    double entry_price;
    double exit_price;
    double size;
    double pnl_usd;
    double pnl_r;
    ExitReason exit_reason;
    int hold_duration_ms;
    uint64_t entry_ts;
    uint64_t exit_ts;
    
    // Gate conditions at entry
    double vol_at_entry;
    double imbalance_at_entry;
    int displacement_at_entry;
};

/**
 * Daily statistics
 */
struct BurstDailyStats {
    int trades_taken = 0;
    int wins = 0;
    int losses = 0;
    double total_pnl_usd = 0.0;
    double total_pnl_r = 0.0;
    double max_drawdown_usd = 0.0;
    double running_high_usd = 0.0;
    uint64_t last_reset_ts = 0;
    
    double win_rate() const noexcept {
        return trades_taken > 0 ? (double)wins / trades_taken : 0.0;
    }
    
    double expectancy_r() const noexcept {
        return trades_taken > 0 ? total_pnl_r / trades_taken : 0.0;
    }
    
    void reset() noexcept {
        trades_taken = 0;
        wins = 0;
        losses = 0;
        total_pnl_usd = 0.0;
        total_pnl_r = 0.0;
        max_drawdown_usd = 0.0;
        running_high_usd = 0.0;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        last_reset_ts = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SIGNAL STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Entry signal (generated when gate opens)
 */
struct BurstEntrySignal {
    BurstSymbol symbol;
    Direction direction;
    double suggested_size;
    double entry_price;                     // Current mid or aggressive
    double stop_price;                      // Based on max adverse R
    
    // Supporting metrics
    double vol_expansion;
    double imbalance_ratio;
    int displacement_ticks;
    double edge_bps;
    double cost_bps;
    
    uint64_t generated_ts;
    
    bool is_valid() const noexcept {
        return direction != Direction::NONE && suggested_size > 0;
    }
};

/**
 * Exit signal
 */
struct BurstExitSignal {
    BurstSymbol symbol;
    ExitReason reason;
    double exit_price;
    uint64_t generated_ts;
};

// ═══════════════════════════════════════════════════════════════════════════════
// CALLBACK TYPES
// ═══════════════════════════════════════════════════════════════════════════════

using OnBurstEntrySignal = std::function<void(const BurstEntrySignal&)>;
using OnBurstExitSignal = std::function<void(const BurstExitSignal&)>;
using OnBurstTradeResult = std::function<void(const BurstTradeResult&)>;
using OnBurstIdleLog = std::function<void(BurstSymbol, const GateStatus&)>;

// ═══════════════════════════════════════════════════════════════════════════════
// INTERNAL METRICS STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

struct VolatilityMetrics {
    double current_vol = 0.0;               // Current 5-min realized vol
    double trailing_median = 0.0;           // 30-min trailing median
    double expansion_ratio = 1.0;           // current / median
    uint64_t last_update_ts = 0;
};

struct ImbalanceMetrics {
    double bid_qty_top_n = 0.0;
    double ask_qty_top_n = 0.0;
    double imbalance_ratio = 0.5;           // bid / (bid + ask)
    Direction inferred_direction = Direction::NONE;
    uint64_t last_update_ts = 0;
};

struct SpreadMetrics {
    double current_spread_bps = 0.0;
    double percentile_30min = 50.0;
    std::deque<double> spread_history;
    uint64_t last_update_ts = 0;
    
    static constexpr size_t MAX_HISTORY = 18000; // ~30 min at 100ms updates
};

struct DisplacementMetrics {
    double anchor_price = 0.0;
    double current_price = 0.0;
    double price_move = 0.0;
    int ticks_moved = 0;
    uint64_t anchor_ts = 0;
    uint64_t last_update_ts = 0;
    
    static constexpr uint64_t ANCHOR_STALE_US = 5'000'000; // 5 seconds
};

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN ENGINE CLASS
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * CryptoBurstEngine - The Opportunistic Crypto Trading Engine
 * 
 * DESIGN:
 * - Normally OFF (idle)
 * - Turns ON only when ALL pre-gate conditions align simultaneously
 * - Takes a single position in the direction of imbalance
 * - Exits on: time stop, structure break, or max adverse
 * - Enforces HARD cooldown (no exceptions, no overrides)
 * 
 * Thread safety: 
 * - on_book_update() and on_trade() may be called from WebSocket thread
 * - All other methods are safe to call from any thread
 * - Internal state protected by minimal locking
 */
class CryptoBurstEngine {
public:
    // ═══════════════════════════════════════════════════════════════════════
    // CONSTRUCTION
    // ═══════════════════════════════════════════════════════════════════════
    
    explicit CryptoBurstEngine(const BurstEngineConfig& config)
        : config_(config)
        , running_(false)
    {
        // Initialize per-symbol state
        for (const auto& sym_cfg : config_.symbols) {
            symbol_configs_[sym_cfg.symbol] = sym_cfg;
            positions_[sym_cfg.symbol] = BurstPosition{};
            cooldown_until_[sym_cfg.symbol] = 0;
            vol_metrics_[sym_cfg.symbol] = VolatilityMetrics{};
            imbalance_metrics_[sym_cfg.symbol] = ImbalanceMetrics{};
            spread_metrics_[sym_cfg.symbol] = SpreadMetrics{};
            displacement_metrics_[sym_cfg.symbol] = DisplacementMetrics{};
            current_regimes_[sym_cfg.symbol] = Regime::UNKNOWN;
            latest_books_[sym_cfg.symbol] = BurstBook{};
        }
        
        daily_stats_.reset();
        last_idle_log_ts_ = now_us();
    }
    
    ~CryptoBurstEngine() {
        stop();
    }
    
    // Non-copyable, non-movable
    CryptoBurstEngine(const CryptoBurstEngine&) = delete;
    CryptoBurstEngine& operator=(const CryptoBurstEngine&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    void start() {
        bool expected = false;
        if (running_.compare_exchange_strong(expected, true)) {
            std::cout << "[CRYPTO-BURST] Engine started\n";
            std::cout << "[CRYPTO-BURST] BTCUSDT=LIVE, ETHUSDT/SOLUSDT=SHADOW\n";
            std::cout << "[CRYPTO-BURST] Gate armed. Waiting for burst conditions...\n";
        }
    }
    
    void stop() {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false)) {
            std::cout << "[CRYPTO-BURST] Engine stopped\n";
        }
    }
    
    bool is_running() const noexcept { 
        return running_.load(std::memory_order_relaxed); 
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DATA FEED (Call from WebSocket handlers)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Feed order book update
     * Call this on every depth snapshot from Binance WebSocket
     */
    void on_book_update(const BurstBook& book) {
        if (!running_.load(std::memory_order_relaxed)) return;
        if (!book.is_valid()) return;
        
        BurstSymbol symbol = book.symbol;
        auto it = symbol_configs_.find(symbol);
        if (it == symbol_configs_.end()) return;
        
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            latest_books_[symbol] = book;
            
            // Update derived metrics
            update_imbalance(symbol, book);
            update_spread_metrics(symbol, book);
            update_displacement(symbol, book.mid());
            
            // Update regime detection
            current_regimes_[symbol] = detect_regime(symbol);
        }
        
        // Check for entry signals (only if not in position)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!positions_[symbol].is_open()) {
                auto signal = generate_entry_signal(symbol);
                if (signal && on_entry_signal_) {
                    on_entry_signal_(*signal);
                }
            }
        }
        
        // Check for exit signals (if in position)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (positions_[symbol].is_open()) {
                // Update position current price
                positions_[symbol].current_price = book.mid();
                
                // Calculate unrealized PnL
                double dir_mult = (positions_[symbol].direction == Direction::LONG) ? 1.0 : -1.0;
                positions_[symbol].unrealized_pnl = 
                    (positions_[symbol].current_price - positions_[symbol].entry_price) * 
                    positions_[symbol].size * dir_mult;
                
                // Track max adverse
                if (positions_[symbol].unrealized_pnl < positions_[symbol].max_adverse_pnl) {
                    positions_[symbol].max_adverse_pnl = positions_[symbol].unrealized_pnl;
                }
                
                auto exit_signal = check_exit_conditions(symbol);
                if (exit_signal && on_exit_signal_) {
                    on_exit_signal_(*exit_signal);
                }
            }
        }
        
        // Maybe log idle state
        maybe_log_idle_state(symbol);
    }
    
    /**
     * Feed aggregate trade update
     * Call this on every aggTrade from Binance WebSocket
     */
    void on_trade(const BurstTrade& trade) {
        if (!running_.load(std::memory_order_relaxed)) return;
        
        BurstSymbol symbol = trade.symbol;
        auto it = symbol_configs_.find(symbol);
        if (it == symbol_configs_.end()) return;
        
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            
            // Add to trade history for volatility calculation
            trade_history_[symbol].push_back(trade);
            if (trade_history_[symbol].size() > MAX_TRADE_HISTORY) {
                trade_history_[symbol].pop_front();
            }
            
            // Update volatility
            update_volatility(symbol);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CALLBACKS (Set before start())
    // ═══════════════════════════════════════════════════════════════════════
    
    void set_on_entry_signal(OnBurstEntrySignal cb) { on_entry_signal_ = std::move(cb); }
    void set_on_exit_signal(OnBurstExitSignal cb) { on_exit_signal_ = std::move(cb); }
    void set_on_trade_result(OnBurstTradeResult cb) { on_trade_result_ = std::move(cb); }
    void set_on_idle_log(OnBurstIdleLog cb) { on_idle_log_ = std::move(cb); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // EXECUTION FEEDBACK (Call after order fills)
    // ═══════════════════════════════════════════════════════════════════════
    
    void on_entry_fill(BurstSymbol symbol, Direction direction, 
                       double fill_price, double fill_size) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        auto& pos = positions_[symbol];
        pos.symbol = symbol;
        pos.direction = direction;
        pos.entry_price = fill_price;
        pos.size = fill_size;
        pos.current_price = fill_price;
        pos.unrealized_pnl = 0.0;
        pos.max_adverse_pnl = 0.0;
        pos.entry_ts = now_us();
        
        // Capture gate conditions at entry
        {
            std::lock_guard<std::mutex> data_lock(data_mutex_);
            pos.vol_at_entry = vol_metrics_[symbol].expansion_ratio;
            pos.imbalance_at_entry = imbalance_metrics_[symbol].imbalance_ratio;
            pos.displacement_at_entry = displacement_metrics_[symbol].ticks_moved;
        }
        
        std::cout << "[CRYPTO-BURST] ENTRY: " << symbol_str(symbol)
                  << " " << direction_str(direction)
                  << " @ " << std::fixed << std::setprecision(2) << fill_price
                  << " size=" << std::setprecision(6) << fill_size << "\n";
    }
    
    void on_exit_fill(BurstSymbol symbol, double fill_price, ExitReason reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        auto& pos = positions_[symbol];
        if (!pos.is_open()) return;
        
        // Calculate final PnL
        double dir_mult = (pos.direction == Direction::LONG) ? 1.0 : -1.0;
        double pnl_usd = (fill_price - pos.entry_price) * pos.size * dir_mult;
        
        // Create trade result
        BurstTradeResult result;
        result.symbol = symbol;
        result.direction = pos.direction;
        result.entry_price = pos.entry_price;
        result.exit_price = fill_price;
        result.size = pos.size;
        result.pnl_usd = pnl_usd;
        result.pnl_r = pnl_usd / (pos.entry_price * pos.size * 0.005); // Assume 0.5% risk
        result.exit_reason = reason;
        result.hold_duration_ms = pos.hold_duration_ms();
        result.entry_ts = pos.entry_ts;
        result.exit_ts = now_us();
        result.vol_at_entry = pos.vol_at_entry;
        result.imbalance_at_entry = pos.imbalance_at_entry;
        result.displacement_at_entry = pos.displacement_at_entry;
        
        // Update daily stats
        daily_stats_.trades_taken++;
        if (pnl_usd > 0) daily_stats_.wins++;
        else daily_stats_.losses++;
        daily_stats_.total_pnl_usd += pnl_usd;
        daily_stats_.total_pnl_r += result.pnl_r;
        
        // Track drawdown
        if (daily_stats_.total_pnl_usd > daily_stats_.running_high_usd) {
            daily_stats_.running_high_usd = daily_stats_.total_pnl_usd;
        }
        double current_dd = daily_stats_.running_high_usd - daily_stats_.total_pnl_usd;
        if (current_dd > daily_stats_.max_drawdown_usd) {
            daily_stats_.max_drawdown_usd = current_dd;
        }
        
        trade_log_.push_back(result);
        
        std::cout << "[CRYPTO-BURST] EXIT: " << symbol_str(symbol)
                  << " @ " << std::fixed << std::setprecision(2) << fill_price
                  << " PnL=$" << std::setprecision(2) << pnl_usd
                  << " (" << std::setprecision(2) << result.pnl_r << "R)"
                  << " reason=" << exit_str(reason)
                  << " hold=" << result.hold_duration_ms << "ms\n";
        
        // Start cooldown
        bool was_winner = (pnl_usd > 0);
        start_cooldown(symbol, was_winner);
        
        // Clear position
        pos = BurstPosition{};
        
        // Callback
        if (on_trade_result_) {
            on_trade_result_(result);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY STATE
    // ═══════════════════════════════════════════════════════════════════════
    
    GateStatus get_gate_status(BurstSymbol symbol) const {
        return evaluate_gate(symbol);
    }
    
    std::optional<BurstPosition> get_position(BurstSymbol symbol) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = positions_.find(symbol);
        if (it != positions_.end() && it->second.is_open()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    BurstDailyStats get_daily_stats() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return daily_stats_;
    }
    
    bool is_in_cooldown(BurstSymbol symbol) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = cooldown_until_.find(symbol);
        if (it == cooldown_until_.end()) return false;
        return now_us() < it->second;
    }
    
    int seconds_until_cooldown_end(BurstSymbol symbol) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = cooldown_until_.find(symbol);
        if (it == cooldown_until_.end()) return 0;
        
        uint64_t now = now_us();
        if (now >= it->second) return 0;
        
        return static_cast<int>((it->second - now) / 1'000'000);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MANUAL CONTROLS
    // ═══════════════════════════════════════════════════════════════════════
    
    void force_exit(BurstSymbol symbol) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto& pos = positions_[symbol];
        if (!pos.is_open()) return;
        
        BurstExitSignal signal;
        signal.symbol = symbol;
        signal.reason = ExitReason::MANUAL;
        signal.exit_price = pos.current_price;
        signal.generated_ts = now_us();
        
        std::cout << "[CRYPTO-BURST] FORCE EXIT: " << symbol_str(symbol) << "\n";
        
        if (on_exit_signal_) {
            on_exit_signal_(signal);
        }
    }
    
    void reset_daily_stats() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        daily_stats_.reset();
        std::cout << "[CRYPTO-BURST] Daily stats reset\n";
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // METRICS CALCULATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void update_volatility(BurstSymbol symbol) {
        // Must hold data_mutex_
        auto& history = trade_history_[symbol];
        if (history.size() < 100) return;
        
        uint64_t now = now_us();
        uint64_t cutoff_5min = now - 5 * 60 * 1'000'000ULL;
        uint64_t cutoff_30min = now - 30 * 60 * 1'000'000ULL;
        
        std::vector<double> returns_5min;
        std::vector<double> abs_returns_30min;
        double prev_price = 0;
        
        for (const auto& trade : history) {
            if (trade.local_ts >= cutoff_30min) {
                if (prev_price > 0) {
                    double ret = (trade.price - prev_price) / prev_price;
                    abs_returns_30min.push_back(std::abs(ret));
                    
                    if (trade.local_ts >= cutoff_5min) {
                        returns_5min.push_back(ret);
                    }
                }
                prev_price = trade.price;
            }
        }
        
        if (returns_5min.size() < 10 || abs_returns_30min.size() < 50) return;
        
        // Current volatility (5-min realized)
        double sum_sq = 0;
        for (double r : returns_5min) {
            sum_sq += r * r;
        }
        double current_vol = std::sqrt(sum_sq / returns_5min.size()) * std::sqrt(returns_5min.size());
        
        // Trailing median (30-min)
        std::sort(abs_returns_30min.begin(), abs_returns_30min.end());
        double median_vol = abs_returns_30min[abs_returns_30min.size() / 2];
        
        // Normalize median to comparable scale
        median_vol *= std::sqrt(50.0); // Approximate scaling
        
        auto& metrics = vol_metrics_[symbol];
        metrics.current_vol = current_vol;
        metrics.trailing_median = median_vol;
        metrics.expansion_ratio = (median_vol > 0) ? (current_vol / median_vol) : 1.0;
        metrics.last_update_ts = now;
    }
    
    void update_imbalance(BurstSymbol symbol, const BurstBook& book) {
        // Must hold data_mutex_
        const int TOP_N = 10;
        
        double bid_qty = 0, ask_qty = 0;
        for (int i = 0; i < std::min(TOP_N, (int)book.bid_levels); ++i) {
            bid_qty += book.bids[i].qty;
        }
        for (int i = 0; i < std::min(TOP_N, (int)book.ask_levels); ++i) {
            ask_qty += book.asks[i].qty;
        }
        
        double total = bid_qty + ask_qty;
        double ratio = (total > 0) ? (bid_qty / total) : 0.5;
        
        auto& metrics = imbalance_metrics_[symbol];
        metrics.bid_qty_top_n = bid_qty;
        metrics.ask_qty_top_n = ask_qty;
        metrics.imbalance_ratio = ratio;
        metrics.inferred_direction = (ratio >= 0.65) ? Direction::LONG :
                                     (ratio <= 0.35) ? Direction::SHORT :
                                     Direction::NONE;
        metrics.last_update_ts = now_us();
    }
    
    void update_spread_metrics(BurstSymbol symbol, const BurstBook& book) {
        // Must hold data_mutex_
        auto& metrics = spread_metrics_[symbol];
        
        double current_spread = book.spread_bps();
        metrics.current_spread_bps = current_spread;
        
        metrics.spread_history.push_back(current_spread);
        while (metrics.spread_history.size() > SpreadMetrics::MAX_HISTORY) {
            metrics.spread_history.pop_front();
        }
        
        if (metrics.spread_history.size() >= 100) {
            std::vector<double> sorted(metrics.spread_history.begin(), 
                                       metrics.spread_history.end());
            std::sort(sorted.begin(), sorted.end());
            
            auto it = std::lower_bound(sorted.begin(), sorted.end(), current_spread);
            size_t pos = std::distance(sorted.begin(), it);
            metrics.percentile_30min = (double)pos / sorted.size() * 100.0;
        } else {
            metrics.percentile_30min = 50.0;
        }
        
        metrics.last_update_ts = now_us();
    }
    
    void update_displacement(BurstSymbol symbol, double price) {
        // Must hold data_mutex_
        auto& metrics = displacement_metrics_[symbol];
        uint64_t now = now_us();
        
        auto it = symbol_configs_.find(symbol);
        if (it == symbol_configs_.end()) return;
        const auto& cfg = it->second;
        
        // Reset anchor if stale
        if (metrics.anchor_ts == 0 || 
            (now - metrics.anchor_ts) > DisplacementMetrics::ANCHOR_STALE_US) {
            metrics.anchor_price = price;
            metrics.anchor_ts = now;
        }
        
        metrics.current_price = price;
        metrics.price_move = std::abs(price - metrics.anchor_price);
        metrics.ticks_moved = static_cast<int>(metrics.price_move / cfg.tick_size);
        metrics.last_update_ts = now;
    }
    
    Regime detect_regime(BurstSymbol symbol) const {
        // Must hold data_mutex_
        // Simple regime detection based on volatility and imbalance
        auto vol_it = vol_metrics_.find(symbol);
        auto imbal_it = imbalance_metrics_.find(symbol);
        
        if (vol_it == vol_metrics_.end() || imbal_it == imbal_metrics_.end()) {
            return Regime::UNKNOWN;
        }
        
        double vol_exp = vol_it->second.expansion_ratio;
        double imbal = imbal_it->second.imbalance_ratio;
        double imbal_strength = std::abs(imbal - 0.5) * 2.0; // 0 = balanced, 1 = max imbalance
        
        if (vol_exp >= 2.0 && imbal_strength >= 0.30) {
            return Regime::TRENDING;
        } else if (vol_exp >= 1.5 || imbal_strength >= 0.20) {
            return Regime::TRANSITION;
        } else {
            return Regime::RANGING;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // GATE EVALUATION
    // ═══════════════════════════════════════════════════════════════════════
    
    GateStatus evaluate_gate(BurstSymbol symbol) const {
        GateStatus status;
        
        auto sym_it = symbol_configs_.find(symbol);
        if (sym_it == symbol_configs_.end()) {
            status.primary_block = GateBlock::SYMBOL_SHADOW_ONLY;
            return status;
        }
        const auto& sym_cfg = sym_it->second;
        
        // Check symbol mode (shadow vs live)
        if (sym_cfg.mode == BurstMode::SHADOW) {
            status.primary_block = GateBlock::SYMBOL_SHADOW_ONLY;
            return status;
        }
        
        // Check daily limits
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (daily_stats_.total_pnl_usd <= -config_.daily_loss_limit_usd) {
                status.primary_block = GateBlock::DAILY_LOSS_LIMIT;
                return status;
            }
            status.daily_ok = true;
            
            if (daily_stats_.trades_taken >= config_.max_daily_trades) {
                status.primary_block = GateBlock::MAX_DAILY_TRADES;
                return status;
            }
            status.max_trades_ok = true;
            
            // Check position
            auto pos_it = positions_.find(symbol);
            if (pos_it != positions_.end() && pos_it->second.is_open()) {
                status.primary_block = GateBlock::ALREADY_IN_POSITION;
                return status;
            }
            status.no_position_ok = true;
            
            // Check cooldown
            auto cd_it = cooldown_until_.find(symbol);
            if (cd_it != cooldown_until_.end() && now_us() < cd_it->second) {
                status.seconds_until_cooldown_end = static_cast<int>((cd_it->second - now_us()) / 1'000'000);
                status.primary_block = GateBlock::COOLDOWN_ACTIVE;
                return status;
            }
            status.cooldown_ok = true;
            status.seconds_until_cooldown_end = 0;
        }
        
        // Check market conditions
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            
            // Volatility expansion
            auto vol_it = vol_metrics_.find(symbol);
            if (vol_it != vol_metrics_.end()) {
                status.vol_expansion = vol_it->second.expansion_ratio;
                status.vol_ok = (status.vol_expansion >= config_.gate.vol_expansion_min);
            }
            if (!status.vol_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::VOL_EXPANSION_LOW;
            }
            
            // Spread compression
            auto spread_it = spread_metrics_.find(symbol);
            if (spread_it != spread_metrics_.end()) {
                status.spread_percentile = spread_it->second.percentile_30min;
                status.spread_ok = (status.spread_percentile <= config_.gate.spread_percentile_max);
            }
            if (!status.spread_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::SPREAD_TOO_WIDE;
            }
            
            // Book imbalance
            auto imbal_it = imbalance_metrics_.find(symbol);
            if (imbal_it != imbalance_metrics_.end()) {
                status.imbalance_ratio = imbal_it->second.imbalance_ratio;
                double max_ratio = std::max(status.imbalance_ratio, 1.0 - status.imbalance_ratio);
                status.imbalance_ok = (max_ratio >= config_.gate.imbalance_ratio_min);
            }
            if (!status.imbalance_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::IMBALANCE_WEAK;
            }
            
            // Displacement
            auto disp_it = displacement_metrics_.find(symbol);
            if (disp_it != displacement_metrics_.end()) {
                status.displacement_ticks = disp_it->second.ticks_moved;
                status.displacement_ok = (status.displacement_ticks >= config_.gate.displacement_ticks_min);
            }
            if (!status.displacement_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::DISPLACEMENT_LOW;
            }
            
            // Regime
            auto regime_it = current_regimes_.find(symbol);
            if (regime_it != current_regimes_.end()) {
                status.current_regime = regime_it->second;
                status.regime_ok = (status.current_regime == config_.gate.required_regime);
            }
            if (!status.regime_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::REGIME_NOT_TRENDING;
            }
            
            // Edge vs cost
            double edge_bps = calculate_edge(symbol);
            double cost_bps = calculate_cost(symbol);
            status.edge_to_cost = (cost_bps > 0) ? (edge_bps / cost_bps) : 0.0;
            status.edge_ok = (status.edge_to_cost >= config_.gate.edge_to_cost_min);
            if (!status.edge_ok && status.primary_block == GateBlock::NONE) {
                status.primary_block = GateBlock::EDGE_LESS_THAN_COST;
            }
        }
        
        return status;
    }
    
    double calculate_edge(BurstSymbol symbol) const {
        // Must hold data_mutex_
        // Edge = expected price move based on imbalance and displacement
        auto disp_it = displacement_metrics_.find(symbol);
        auto book_it = latest_books_.find(symbol);
        
        if (disp_it == disp_metrics_.end() || book_it == latest_books_.end()) {
            return 0.0;
        }
        
        double mid = book_it->second.mid();
        if (mid <= 0) return 0.0;
        
        double displacement_bps = (disp_it->second.price_move / mid) * 10000.0;
        return displacement_bps;
    }
    
    double calculate_cost(BurstSymbol symbol) const {
        // Must hold data_mutex_
        // Cost = spread + fees (round trip)
        auto book_it = latest_books_.find(symbol);
        auto sym_it = symbol_configs_.find(symbol);
        
        if (book_it == latest_books_.end() || sym_it == symbol_configs_.end()) {
            return 999.0; // High cost to block
        }
        
        double spread_bps = book_it->second.spread_bps();
        double fees_bps = sym_it->second.taker_fee_bps * 2; // Round trip
        
        return spread_bps + fees_bps;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // SIGNAL GENERATION
    // ═══════════════════════════════════════════════════════════════════════
    
    std::optional<BurstEntrySignal> generate_entry_signal(BurstSymbol symbol) {
        // Must hold state_mutex_ and will acquire data_mutex_
        GateStatus status = evaluate_gate(symbol);
        
        if (!status.all_clear()) {
            return std::nullopt;
        }
        
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        auto sym_it = symbol_configs_.find(symbol);
        auto book_it = latest_books_.find(symbol);
        auto imbal_it = imbalance_metrics_.find(symbol);
        
        if (sym_it == symbol_configs_.end() || 
            book_it == latest_books_.end() ||
            imbal_it == imbalance_metrics_.end()) {
            return std::nullopt;
        }
        
        const auto& sym_cfg = sym_it->second;
        const auto& book = book_it->second;
        const auto& imbal = imbal_it->second;
        
        // Determine direction from imbalance
        Direction dir = imbal.inferred_direction;
        if (dir == Direction::NONE) {
            return std::nullopt;
        }
        
        BurstEntrySignal signal;
        signal.symbol = symbol;
        signal.direction = dir;
        signal.suggested_size = sym_cfg.base_size;
        signal.entry_price = (dir == Direction::LONG) ? book.best_ask() : book.best_bid();
        
        // Stop price based on max adverse R (0.5R)
        double risk_amount = signal.entry_price * signal.suggested_size * 0.005; // 0.5% risk
        double stop_distance = risk_amount / signal.suggested_size * config_.exit.max_adverse_r;
        signal.stop_price = (dir == Direction::LONG) ? 
            signal.entry_price - stop_distance : signal.entry_price + stop_distance;
        
        signal.vol_expansion = status.vol_expansion;
        signal.imbalance_ratio = status.imbalance_ratio;
        signal.displacement_ticks = status.displacement_ticks;
        signal.edge_bps = calculate_edge(symbol);
        signal.cost_bps = calculate_cost(symbol);
        signal.generated_ts = now_us();
        
        std::cout << "[CRYPTO-BURST] SIGNAL: " << symbol_str(symbol)
                  << " " << direction_str(dir)
                  << " vol=" << std::fixed << std::setprecision(2) << signal.vol_expansion << "x"
                  << " imbal=" << std::setprecision(0) << (signal.imbalance_ratio * 100) << "/"
                  << (100 - signal.imbalance_ratio * 100)
                  << " disp=" << signal.displacement_ticks << "t"
                  << " edge=" << std::setprecision(1) << signal.edge_bps << "bps\n";
        
        return signal;
    }
    
    std::optional<BurstExitSignal> check_exit_conditions(BurstSymbol symbol) {
        // Must hold state_mutex_
        auto pos_it = positions_.find(symbol);
        if (pos_it == positions_.end() || !pos_it->second.is_open()) {
            return std::nullopt;
        }
        
        const auto& pos = pos_it->second;
        
        // Check time stop
        int hold_ms = pos.hold_duration_ms();
        if (hold_ms >= config_.exit.time_stop_max_sec * 1000) {
            BurstExitSignal signal;
            signal.symbol = symbol;
            signal.reason = ExitReason::TIME_STOP;
            signal.exit_price = pos.current_price;
            signal.generated_ts = now_us();
            return signal;
        }
        
        // Check max adverse (only after min hold time)
        if (hold_ms >= config_.exit.time_stop_min_sec * 1000) {
            double risk_amount = pos.entry_price * pos.size * 0.005;
            double adverse_r = pos.max_adverse_pnl / risk_amount;
            
            if (adverse_r <= -config_.exit.max_adverse_r) {
                BurstExitSignal signal;
                signal.symbol = symbol;
                signal.reason = ExitReason::MAX_ADVERSE;
                signal.exit_price = pos.current_price;
                signal.generated_ts = now_us();
                return signal;
            }
        }
        
        // Check structure break (imbalance collapse)
        if (config_.exit.structure_break_exit) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            auto imbal_it = imbalance_metrics_.find(symbol);
            if (imbal_it != imbalance_metrics_.end()) {
                double imbal = imbal_it->second.imbalance_ratio;
                double max_imbal = std::max(imbal, 1.0 - imbal);
                
                // If imbalance collapsed to near 50/50
                if (max_imbal <= config_.exit.imbalance_collapse_threshold) {
                    BurstExitSignal signal;
                    signal.symbol = symbol;
                    signal.reason = ExitReason::STRUCTURE_BREAK;
                    signal.exit_price = pos.current_price;
                    signal.generated_ts = now_us();
                    return signal;
                }
            }
        }
        
        return std::nullopt;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // COOLDOWN MANAGEMENT
    // ═══════════════════════════════════════════════════════════════════════
    
    void start_cooldown(BurstSymbol symbol, bool was_winner) {
        // Must hold state_mutex_
        int cooldown_sec = was_winner ? 
            config_.cooldown.cooldown_after_win_sec :
            config_.cooldown.cooldown_after_loss_sec;
        
        cooldown_until_[symbol] = now_us() + (cooldown_sec * 1'000'000ULL);
        
        std::cout << "[CRYPTO-BURST] Cooldown: " << symbol_str(symbol)
                  << " for " << cooldown_sec << "s"
                  << " (" << (was_winner ? "win" : "loss") << ")\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // IDLE LOGGING
    // ═══════════════════════════════════════════════════════════════════════
    
    void maybe_log_idle_state(BurstSymbol symbol) {
        if (!config_.log_idle_state) return;
        
        uint64_t now = now_us();
        uint64_t interval_us = config_.idle_log_interval_sec * 1'000'000ULL;
        
        if (now - last_idle_log_ts_ < interval_us) return;
        last_idle_log_ts_ = now;
        
        GateStatus status = evaluate_gate(symbol);
        
        if (!status.all_clear()) {
            std::cout << status.to_log_string() << "\n";
            
            if (on_idle_log_) {
                on_idle_log_(symbol, status);
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UTILITY
    // ═══════════════════════════════════════════════════════════════════════
    
    static uint64_t now_us() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER VARIABLES
    // ═══════════════════════════════════════════════════════════════════════
    
    // Configuration
    BurstEngineConfig config_;
    std::unordered_map<BurstSymbol, BurstSymbolConfig> symbol_configs_;
    
    // State
    std::atomic<bool> running_;
    
    // Per-symbol state (protected by state_mutex_)
    mutable std::mutex state_mutex_;
    std::unordered_map<BurstSymbol, BurstPosition> positions_;
    std::unordered_map<BurstSymbol, uint64_t> cooldown_until_;
    BurstDailyStats daily_stats_;
    std::vector<BurstTradeResult> trade_log_;
    
    // Market data state (protected by data_mutex_)
    mutable std::mutex data_mutex_;
    std::unordered_map<BurstSymbol, BurstBook> latest_books_;
    std::unordered_map<BurstSymbol, VolatilityMetrics> vol_metrics_;
    std::unordered_map<BurstSymbol, ImbalanceMetrics> imbalance_metrics_;
    std::unordered_map<BurstSymbol, SpreadMetrics> spread_metrics_;
    std::unordered_map<BurstSymbol, DisplacementMetrics> displacement_metrics_;
    std::unordered_map<BurstSymbol, Regime> current_regimes_;
    
    // Trade history for volatility calculation
    std::unordered_map<BurstSymbol, std::deque<BurstTrade>> trade_history_;
    static constexpr size_t MAX_TRADE_HISTORY = 10000;
    
    // Callbacks
    OnBurstEntrySignal on_entry_signal_;
    OnBurstExitSignal on_exit_signal_;
    OnBurstTradeResult on_trade_result_;
    OnBurstIdleLog on_idle_log_;
    
    // Idle logging timestamp
    uint64_t last_idle_log_ts_ = 0;
};

} // namespace burst
} // namespace crypto
} // namespace chimera
