// =============================================================================
// CryptoRuleset.hpp - v4.5.0 - OFFICIAL CRYPTO ENGINE TRADING RULESET
// =============================================================================
// PURPOSE: Enforce ALL rules for CryptoEngine trading
//          CryptoEngine is OFF by default and activates ONLY when ALL gates pass
//
// DESIGN PRINCIPLES:
//   1. DISABLED BY DEFAULT - Requires explicit enable + shadow validation
//   2. EPISODIC ALPHA ONLY - Not general-purpose trading
//   3. COMPLETE ISOLATION - Cannot affect Income/CFD engines
//   4. FIXED RISK - No dynamic sizing, no scaling, no martingale
//
// ALLOWED TRADE CLASSES:
//   CLASS A: Liquidity Vacuum Capture (PRIMARY) - Speed-based edge
//   CLASS B: Momentum Continuation (SECONDARY) - Flow-based edge
//   ALL OTHERS: DISALLOWED
//
// GLOBAL ACTIVATION GATES (ALL MUST PASS):
//   G1: Infrastructure/speed gate (latency, packet loss)
//   G2: Market quality gate (spread, depth, book health)
//   G3: Volatility gate (impulse detection, vol cap)
//   G4: Cross-asset stress gate (crypto stress, equity stress, income exposure)
//   G5: Self-discipline gate (daily PnL, loss streak, trade count)
//
// SYMBOL SCOPE: BTCUSDT, ETHUSDT ONLY - NO EXCEPTIONS
// =============================================================================
#pragma once

#include <cstdint>
#include <atomic>
#include <chrono>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace Chimera {
namespace Crypto {

// =============================================================================
// CONSTANTS - HARD LIMITS (NON-NEGOTIABLE)
// =============================================================================
namespace Constants {
    // Symbol scope - STRICT
    constexpr const char* ALLOWED_SYMBOLS[] = {"BTCUSDT", "ETHUSDT"};
    constexpr size_t NUM_ALLOWED_SYMBOLS = 2;
    
    // G1: Infrastructure thresholds
    constexpr double MEDIAN_RTT_LIMIT_MS = 0.5;
    constexpr double P99_RTT_LIMIT_MS = 1.2;
    constexpr uint64_t PACKET_LOSS_WINDOW_NS = 5'000'000'000;  // 5 seconds
    constexpr uint64_t INFRA_DISABLE_DURATION_NS = 60'000'000'000;  // 60 seconds
    
    // G2: Market quality thresholds
    constexpr double SPREAD_MULT_LIMIT = 1.5;  // Max 1.5x session median
    constexpr double BTCUSDT_MIN_DEPTH = 5.0;   // Min BTC at top-of-book
    constexpr double ETHUSDT_MIN_DEPTH = 50.0;  // Min ETH at top-of-book
    
    // G3: Volatility thresholds
    constexpr double VOL_CAP_MULT = 3.0;  // 3σ impulse detection
    constexpr uint64_t IMPULSE_WINDOW_NS = 3'000'000'000;  // 3 seconds
    constexpr uint64_t VOL_DISABLE_DURATION_NS = 60'000'000'000;  // Rest of minute
    
    // G4: Cross-asset stress thresholds
    constexpr double CRYPTO_STRESS_THRESHOLD = 0.7;
    constexpr double EQUITY_STRESS_THRESHOLD = 0.6;
    constexpr double INCOME_EXPOSURE_LIMIT = 0.5;  // 50% max
    
    // G5: Self-discipline limits
    constexpr double DAILY_STOP_USD = -50.0;   // Small, fixed daily loss limit
    constexpr int MAX_LOSS_STREAK = 2;
    constexpr int MAX_TRADES_PER_SESSION = 5;
    
    // Position/Risk limits - FIXED
    constexpr double BTCUSDT_SIZE = 0.001;  // Fixed BTC size per trade
    constexpr double ETHUSDT_SIZE = 0.01;   // Fixed ETH size per trade
    constexpr int MAX_POSITIONS_PER_SYMBOL = 1;
    constexpr int MAX_TRADES_PER_MINUTE = 2;
    constexpr double MAX_LOSS_PER_TRADE_USD = 5.0;  // Small, fixed
    
    // Execution limits
    constexpr double SLIPPAGE_THRESHOLD_BPS = 2.0;
    constexpr uint64_t MAX_HOLD_MS = 2000;  // 2 second max hold
    
    // CLASS A: Liquidity Vacuum parameters
    constexpr double DEPTH_DROP_THRESHOLD = 0.70;  // 70% drop
    constexpr uint64_t DEPTH_DROP_WINDOW_MS = 50;   // Within 50ms
    constexpr uint64_t AGGRESSIVE_PAUSE_MS = 20;    // 20ms pause in flow
    constexpr int CLASS_A_TP_TICKS = 3;
    constexpr int CLASS_A_SL_TICKS = 5;
    constexpr uint64_t CLASS_A_TIMEOUT_MS = 750;
    
    // CLASS B: Momentum Continuation parameters
    constexpr uint64_t IMBALANCE_PERSIST_MS = 300;  // 300ms persistence
    constexpr int CLASS_B_TP_TICKS = 2;
    constexpr int CLASS_B_SL_TICKS = 4;
    constexpr uint64_t CLASS_B_TIMEOUT_MS = 1500;
    
    // Cooldowns
    constexpr uint64_t LOSS_COOLDOWN_MS = 60'000;  // 60s after loss
    constexpr uint64_t TRADE_COOLDOWN_MS = 500;    // 500ms between trades
}

// =============================================================================
// ENUMS
// =============================================================================
enum class RulesetState : uint8_t {
    DISABLED = 0,       // Default state - no trading
    SHADOW = 1,         // Shadow mode - paper trades only
    ARMED = 2,          // All gates passed, ready to trade
    TRADING = 3,        // Active position
    COOLDOWN = 4,       // Post-trade/post-loss cooldown
    BLOCKED = 5         // Gate failed - temporarily disabled
};

enum class TradeClass : uint8_t {
    NONE = 0,
    LIQUIDITY_VACUUM = 1,   // Class A
    MOMENTUM_CONTINUATION = 2  // Class B
};

enum class GateId : uint8_t {
    G1_INFRASTRUCTURE = 1,
    G2_MARKET_QUALITY = 2,
    G3_VOLATILITY = 3,
    G4_CROSS_ASSET = 4,
    G5_DISCIPLINE = 5
};

enum class BlockReason : uint8_t {
    NONE = 0,
    
    // G1 failures
    LATENCY_HIGH,
    PACKET_LOSS,
    
    // G2 failures
    SPREAD_WIDE,
    DEPTH_LOW,
    BOOK_CROSSED,
    
    // G3 failures
    VOL_HIGH,
    IMPULSE_DETECTED,
    
    // G4 failures
    CRYPTO_STRESS,
    EQUITY_STRESS,
    INCOME_EXPOSURE,
    
    // G5 failures
    DAILY_STOP_HIT,
    LOSS_STREAK,
    MAX_TRADES,
    
    // Execution failures
    SLIPPAGE_HIGH,
    SYMBOL_NOT_ALLOWED,
    ALREADY_POSITIONED,
    COOLDOWN_ACTIVE,
    NO_VALID_SETUP,
    SHADOW_MODE,
    DISABLED,
    
    MAX_REASON
};

inline const char* block_reason_str(BlockReason r) {
    switch (r) {
        case BlockReason::NONE: return "NONE";
        case BlockReason::LATENCY_HIGH: return "LATENCY_HIGH";
        case BlockReason::PACKET_LOSS: return "PACKET_LOSS";
        case BlockReason::SPREAD_WIDE: return "SPREAD_WIDE";
        case BlockReason::DEPTH_LOW: return "DEPTH_LOW";
        case BlockReason::BOOK_CROSSED: return "BOOK_CROSSED";
        case BlockReason::VOL_HIGH: return "VOL_HIGH";
        case BlockReason::IMPULSE_DETECTED: return "IMPULSE_DETECTED";
        case BlockReason::CRYPTO_STRESS: return "CRYPTO_STRESS";
        case BlockReason::EQUITY_STRESS: return "EQUITY_STRESS";
        case BlockReason::INCOME_EXPOSURE: return "INCOME_EXPOSURE";
        case BlockReason::DAILY_STOP_HIT: return "DAILY_STOP_HIT";
        case BlockReason::LOSS_STREAK: return "LOSS_STREAK";
        case BlockReason::MAX_TRADES: return "MAX_TRADES";
        case BlockReason::SLIPPAGE_HIGH: return "SLIPPAGE_HIGH";
        case BlockReason::SYMBOL_NOT_ALLOWED: return "SYMBOL_NOT_ALLOWED";
        case BlockReason::ALREADY_POSITIONED: return "ALREADY_POSITIONED";
        case BlockReason::COOLDOWN_ACTIVE: return "COOLDOWN_ACTIVE";
        case BlockReason::NO_VALID_SETUP: return "NO_VALID_SETUP";
        case BlockReason::SHADOW_MODE: return "SHADOW_MODE";
        case BlockReason::DISABLED: return "DISABLED";
        default: return "UNKNOWN";
    }
}

inline const char* trade_class_str(TradeClass c) {
    switch (c) {
        case TradeClass::NONE: return "NONE";
        case TradeClass::LIQUIDITY_VACUUM: return "LIQUIDITY_VACUUM";
        case TradeClass::MOMENTUM_CONTINUATION: return "MOMENTUM_CONTINUATION";
        default: return "UNKNOWN";
    }
}

inline const char* ruleset_state_str(RulesetState s) {
    switch (s) {
        case RulesetState::DISABLED: return "DISABLED";
        case RulesetState::SHADOW: return "SHADOW";
        case RulesetState::ARMED: return "ARMED";
        case RulesetState::TRADING: return "TRADING";
        case RulesetState::COOLDOWN: return "COOLDOWN";
        case RulesetState::BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// MARKET DATA SNAPSHOT (immutable, passed to ruleset)
// =============================================================================
struct MarketSnapshot {
    const char* symbol = nullptr;
    
    // Prices
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double spread = 0.0;
    double spread_bps = 0.0;
    
    // Depth
    double bid_depth = 0.0;
    double ask_depth = 0.0;
    double total_depth = 0.0;
    
    // Derived
    double imbalance = 0.0;      // (bid_depth - ask_depth) / total
    double prev_bid_depth = 0.0;
    double prev_ask_depth = 0.0;
    double depth_change_pct = 0.0;
    
    // Flow
    int8_t last_aggressor = 0;   // +1 buy, -1 sell
    uint64_t aggressor_pause_ms = 0;
    
    // Timestamps
    uint64_t timestamp_ns = 0;
    uint64_t event_time_ns = 0;
    
    // Quality
    double session_median_spread = 0.0;
    double realized_vol_bps = 0.0;
    double momentum_bps = 0.0;
};

// =============================================================================
// LATENCY TRACKER (for G1)
// =============================================================================
class LatencyTracker {
public:
    static constexpr size_t WINDOW_SIZE = 100;
    
    void record(double rtt_ms) {
        samples_[idx_] = rtt_ms;
        idx_ = (idx_ + 1) % WINDOW_SIZE;
        if (count_ < WINDOW_SIZE) count_++;
        
        // Update running stats
        double sum = 0.0;
        for (size_t i = 0; i < count_; i++) sum += samples_[i];
        median_ = sum / count_;  // Approximation
        
        // Find p99
        if (count_ >= 10) {
            std::array<double, WINDOW_SIZE> sorted;
            std::copy(samples_.begin(), samples_.begin() + count_, sorted.begin());
            std::sort(sorted.begin(), sorted.begin() + count_);
            p99_ = sorted[static_cast<size_t>(count_ * 0.99)];
        }
    }
    
    double median() const { return median_; }
    double p99() const { return p99_; }
    
    void recordPacketLoss() {
        last_packet_loss_ns_ = now_ns();
        packet_loss_count_++;
    }
    
    bool hasRecentPacketLoss() const {
        return (now_ns() - last_packet_loss_ns_) < Constants::PACKET_LOSS_WINDOW_NS;
    }
    
private:
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    std::array<double, WINDOW_SIZE> samples_{};
    size_t idx_ = 0;
    size_t count_ = 0;
    double median_ = 0.0;
    double p99_ = 0.0;
    uint64_t last_packet_loss_ns_ = 0;
    uint64_t packet_loss_count_ = 0;
};

// =============================================================================
// TRADE SIGNAL (output from ruleset)
// =============================================================================
struct TradeSignal {
    bool valid = false;
    TradeClass trade_class = TradeClass::NONE;
    int8_t direction = 0;       // +1 long, -1 short
    double entry_price = 0.0;
    double size = 0.0;
    double tp_price = 0.0;
    double sl_price = 0.0;
    uint64_t timeout_ms = 0;
    const char* symbol = nullptr;
    BlockReason block_reason = BlockReason::NONE;
    
    // Attribution (for logging)
    char entry_reason[64] = {0};
};

// =============================================================================
// CRYPTO RULESET (MAIN CLASS)
// =============================================================================
class CryptoRuleset {
public:
    CryptoRuleset() noexcept {
        reset();
        printf("[CRYPTO-RULESET] Initialized - DEFAULT STATE: DISABLED\n");
        printf("[CRYPTO-RULESET] Allowed symbols: BTCUSDT, ETHUSDT ONLY\n");
        printf("[CRYPTO-RULESET] Trade classes: LIQUIDITY_VACUUM, MOMENTUM_CONTINUATION\n");
    }
    
    // =========================================================================
    // LIFECYCLE
    // =========================================================================
    
    void reset() noexcept {
        state_ = RulesetState::DISABLED;
        enabled_ = false;
        shadow_mode_ = true;
        block_reason_ = BlockReason::DISABLED;
        
        daily_pnl_ = 0.0;
        loss_streak_ = 0;
        trades_today_ = 0;
        wins_today_ = 0;
        
        for (size_t i = 0; i < Constants::NUM_ALLOWED_SYMBOLS; i++) {
            positions_[i] = 0.0;
            entry_prices_[i] = 0.0;
            entry_times_[i] = 0;
            trade_classes_[i] = TradeClass::NONE;
        }
        
        last_trade_ns_ = 0;
        disabled_until_ns_ = 0;
        gate_states_.fill(true);
        
        // Clear stats
        block_counts_.fill(0);
    }
    
    // Enable the ruleset (still starts in SHADOW mode)
    void enable() noexcept {
        enabled_ = true;
        shadow_mode_ = true;  // ALWAYS start shadow
        state_ = RulesetState::SHADOW;
        printf("[CRYPTO-RULESET] ENABLED - Starting in SHADOW mode\n");
        printf("[CRYPTO-RULESET] Minimum 1 week shadow validation required\n");
    }
    
    // Graduate from shadow to live (requires validation)
    bool graduateToLive() noexcept {
        if (!enabled_) {
            printf("[CRYPTO-RULESET] Cannot graduate - not enabled\n");
            return false;
        }
        if (!shadow_validated_) {
            printf("[CRYPTO-RULESET] Cannot graduate - shadow validation incomplete\n");
            return false;
        }
        shadow_mode_ = false;
        state_ = RulesetState::ARMED;
        printf("[CRYPTO-RULESET] GRADUATED TO LIVE - Trading enabled\n");
        return true;
    }
    
    // Mark shadow validation as complete (external validation check)
    void markShadowValidated() noexcept {
        shadow_validated_ = true;
        printf("[CRYPTO-RULESET] Shadow validation COMPLETE\n");
    }
    
    void disable() noexcept {
        enabled_ = false;
        state_ = RulesetState::DISABLED;
        block_reason_ = BlockReason::DISABLED;
        printf("[CRYPTO-RULESET] DISABLED\n");
    }
    
    // =========================================================================
    // STATE ACCESSORS
    // =========================================================================
    
    RulesetState state() const noexcept { return state_; }
    bool isEnabled() const noexcept { return enabled_; }
    bool isShadowMode() const noexcept { return shadow_mode_; }
    bool canTrade() const noexcept { 
        return enabled_ && !shadow_mode_ && state_ == RulesetState::ARMED; 
    }
    BlockReason lastBlockReason() const noexcept { return block_reason_; }
    
    double dailyPnl() const noexcept { return daily_pnl_; }
    int lossStreak() const noexcept { return loss_streak_; }
    int tradesToday() const noexcept { return trades_today_; }
    
    // =========================================================================
    // EXTERNAL STATE SETTERS (for cross-engine coordination)
    // =========================================================================
    
    void setCryptoStress(double stress) noexcept { crypto_stress_ = stress; }
    void setEquityStress(double stress) noexcept { equity_stress_ = stress; }
    void setIncomeExposure(double exposure) noexcept { income_exposure_ = exposure; }
    
    void recordLatency(double rtt_ms) noexcept { latency_tracker_.record(rtt_ms); }
    void recordPacketLoss() noexcept { latency_tracker_.recordPacketLoss(); }
    
    // =========================================================================
    // SYMBOL VALIDATION (STRICT)
    // =========================================================================
    
    [[nodiscard]] bool isSymbolAllowed(const char* symbol) const noexcept {
        if (!symbol) return false;
        for (size_t i = 0; i < Constants::NUM_ALLOWED_SYMBOLS; i++) {
            if (strcmp(symbol, Constants::ALLOWED_SYMBOLS[i]) == 0) {
                return true;
            }
        }
        return false;
    }
    
    [[nodiscard]] int getSymbolIndex(const char* symbol) const noexcept {
        if (!symbol) return -1;
        for (size_t i = 0; i < Constants::NUM_ALLOWED_SYMBOLS; i++) {
            if (strcmp(symbol, Constants::ALLOWED_SYMBOLS[i]) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    
    // =========================================================================
    // GATE EVALUATION (ALL MUST PASS)
    // =========================================================================
    
    [[nodiscard]] bool evaluateAllGates(const MarketSnapshot& snap) noexcept {
        uint64_t now = now_ns();
        
        // Check if temporarily disabled
        if (disabled_until_ns_ > now) {
            block_reason_ = BlockReason::COOLDOWN_ACTIVE;
            return false;
        }
        
        // G1: Infrastructure
        if (!evaluateG1_Infrastructure()) {
            gate_states_[0] = false;
            disableFor(Constants::INFRA_DISABLE_DURATION_NS);
            return false;
        }
        gate_states_[0] = true;
        
        // G2: Market Quality
        if (!evaluateG2_MarketQuality(snap)) {
            gate_states_[1] = false;
            return false;  // Revalidate on next tick
        }
        gate_states_[1] = true;
        
        // G3: Volatility
        if (!evaluateG3_Volatility(snap)) {
            gate_states_[2] = false;
            disableFor(Constants::VOL_DISABLE_DURATION_NS);
            return false;
        }
        gate_states_[2] = true;
        
        // G4: Cross-Asset Stress
        if (!evaluateG4_CrossAsset()) {
            gate_states_[3] = false;
            return false;  // Check again next tick
        }
        gate_states_[3] = true;
        
        // G5: Self-Discipline
        if (!evaluateG5_Discipline()) {
            gate_states_[4] = false;
            return false;  // Off for rest of session
        }
        gate_states_[4] = true;
        
        return true;
    }
    
    // =========================================================================
    // MAIN EVALUATION - RETURNS TRADE SIGNAL OR BLOCK
    // =========================================================================
    
    [[nodiscard]] TradeSignal evaluate(const MarketSnapshot& snap) noexcept {
        TradeSignal signal;
        signal.symbol = snap.symbol;
        
        // =====================================================================
        // PRE-CHECKS
        // =====================================================================
        
        if (!enabled_) {
            signal.block_reason = BlockReason::DISABLED;
            recordBlock(BlockReason::DISABLED);
            return signal;
        }
        
        if (!isSymbolAllowed(snap.symbol)) {
            signal.block_reason = BlockReason::SYMBOL_NOT_ALLOWED;
            recordBlock(BlockReason::SYMBOL_NOT_ALLOWED);
            return signal;
        }
        
        int sym_idx = getSymbolIndex(snap.symbol);
        if (sym_idx < 0) {
            signal.block_reason = BlockReason::SYMBOL_NOT_ALLOWED;
            return signal;
        }
        
        // Check for existing position
        if (positions_[sym_idx] != 0.0) {
            signal.block_reason = BlockReason::ALREADY_POSITIONED;
            recordBlock(BlockReason::ALREADY_POSITIONED);
            return signal;
        }
        
        // Check cooldown
        uint64_t now = now_ns();
        if (now < last_trade_ns_ + Constants::TRADE_COOLDOWN_MS * 1'000'000) {
            signal.block_reason = BlockReason::COOLDOWN_ACTIVE;
            recordBlock(BlockReason::COOLDOWN_ACTIVE);
            return signal;
        }
        
        // =====================================================================
        // EVALUATE ALL GATES
        // =====================================================================
        
        if (!evaluateAllGates(snap)) {
            signal.block_reason = block_reason_;
            return signal;
        }
        
        // =====================================================================
        // SHADOW MODE CHECK
        // =====================================================================
        
        if (shadow_mode_) {
            // Evaluate but don't trade
            auto class_a = evaluateClassA_LiquidityVacuum(snap);
            auto class_b = evaluateClassB_MomentumContinuation(snap);
            
            if (class_a.valid) {
                logShadowSignal(class_a);
                shadow_signals_++;
            } else if (class_b.valid) {
                logShadowSignal(class_b);
                shadow_signals_++;
            }
            
            signal.block_reason = BlockReason::SHADOW_MODE;
            recordBlock(BlockReason::SHADOW_MODE);
            return signal;
        }
        
        // =====================================================================
        // EVALUATE TRADE CLASSES (PRIORITY ORDER)
        // =====================================================================
        
        // CLASS A: Liquidity Vacuum (PRIMARY - best edge)
        TradeSignal class_a = evaluateClassA_LiquidityVacuum(snap);
        if (class_a.valid) {
            class_a.size = getFixedSize(snap.symbol);
            state_ = RulesetState::TRADING;
            return class_a;
        }
        
        // CLASS B: Momentum Continuation (SECONDARY - only if A inactive)
        TradeSignal class_b = evaluateClassB_MomentumContinuation(snap);
        if (class_b.valid) {
            class_b.size = getFixedSize(snap.symbol);
            state_ = RulesetState::TRADING;
            return class_b;
        }
        
        signal.block_reason = BlockReason::NO_VALID_SETUP;
        recordBlock(BlockReason::NO_VALID_SETUP);
        return signal;
    }
    
    // =========================================================================
    // POSITION MANAGEMENT (called by execution layer)
    // =========================================================================
    
    void onFill(const char* symbol, int8_t side, double qty, double price, 
                TradeClass trade_class) noexcept {
        int idx = getSymbolIndex(symbol);
        if (idx < 0) return;
        
        double signed_qty = side > 0 ? qty : -qty;
        
        if (positions_[idx] == 0.0) {
            // Opening position
            positions_[idx] = signed_qty;
            entry_prices_[idx] = price;
            entry_times_[idx] = now_ns();
            trade_classes_[idx] = trade_class;
            trades_today_++;
            last_trade_ns_ = now_ns();
            
            printf("[CRYPTO-RULESET] OPEN: %s %s qty=%.6f @ %.2f class=%s\n",
                   symbol, side > 0 ? "LONG" : "SHORT", qty, price,
                   trade_class_str(trade_class));
        } else {
            // Closing position
            double pnl = 0.0;
            if (positions_[idx] > 0) {
                // Closing long
                pnl = (price - entry_prices_[idx]) * std::abs(positions_[idx]);
            } else {
                // Closing short
                pnl = (entry_prices_[idx] - price) * std::abs(positions_[idx]);
            }
            
            daily_pnl_ += pnl;
            
            // Update win/loss tracking
            if (pnl >= 0) {
                wins_today_++;
                loss_streak_ = 0;
            } else {
                loss_streak_++;
                // Cooldown after loss
                disabled_until_ns_ = now_ns() + Constants::LOSS_COOLDOWN_MS * 1'000'000;
            }
            
            uint64_t hold_ms = (now_ns() - entry_times_[idx]) / 1'000'000;
            
            printf("[CRYPTO-RULESET] CLOSE: %s PnL=$%.4f hold=%lums class=%s streak=%d\n",
                   symbol, pnl, (unsigned long)hold_ms, 
                   trade_class_str(trade_classes_[idx]), loss_streak_);
            
            // Reset position
            positions_[idx] = 0.0;
            entry_prices_[idx] = 0.0;
            entry_times_[idx] = 0;
            trade_classes_[idx] = TradeClass::NONE;
            
            // Update state
            if (loss_streak_ >= Constants::MAX_LOSS_STREAK) {
                state_ = RulesetState::BLOCKED;
                block_reason_ = BlockReason::LOSS_STREAK;
                printf("[CRYPTO-RULESET] ENGINE OFF - Loss streak limit hit\n");
            } else if (daily_pnl_ <= Constants::DAILY_STOP_USD) {
                state_ = RulesetState::BLOCKED;
                block_reason_ = BlockReason::DAILY_STOP_HIT;
                printf("[CRYPTO-RULESET] ENGINE OFF - Daily stop hit\n");
            } else {
                state_ = RulesetState::COOLDOWN;
            }
        }
    }
    
    void onReject(const char* symbol, const char* reason) noexcept {
        printf("[CRYPTO-RULESET] REJECT: %s reason=%s\n", symbol, reason);
        // Could increment failure counter here
    }
    
    void onSlippage(const char* symbol, double slippage_bps) noexcept {
        if (slippage_bps > Constants::SLIPPAGE_THRESHOLD_BPS) {
            printf("[CRYPTO-RULESET] HIGH SLIPPAGE: %s %.2f bps - ENGINE OFF\n",
                   symbol, slippage_bps);
            state_ = RulesetState::BLOCKED;
            block_reason_ = BlockReason::SLIPPAGE_HIGH;
            slippage_kills_++;
        }
    }
    
    // Check for timeout (called periodically)
    bool checkTimeout(const char* symbol, double current_price) noexcept {
        (void)current_price;  // Reserved for future PnL check
        int idx = getSymbolIndex(symbol);
        if (idx < 0 || positions_[idx] == 0.0) return false;
        
        uint64_t hold_ms = (now_ns() - entry_times_[idx]) / 1'000'000;
        uint64_t timeout_ms = trade_classes_[idx] == TradeClass::LIQUIDITY_VACUUM 
            ? Constants::CLASS_A_TIMEOUT_MS 
            : Constants::CLASS_B_TIMEOUT_MS;
        
        if (hold_ms >= timeout_ms) {
            printf("[CRYPTO-RULESET] TIMEOUT: %s after %lums\n", symbol, (unsigned long)hold_ms);
            return true;  // Caller should close position
        }
        return false;
    }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    
    void printStatus() const noexcept {
        printf("\n[CRYPTO-RULESET] Status:\n");
        printf("  State: %s\n", ruleset_state_str(state_));
        printf("  Enabled: %s, Shadow: %s\n", 
               enabled_ ? "YES" : "NO", shadow_mode_ ? "YES" : "NO");
        printf("  Gates: G1=%s G2=%s G3=%s G4=%s G5=%s\n",
               gate_states_[0] ? "PASS" : "FAIL",
               gate_states_[1] ? "PASS" : "FAIL",
               gate_states_[2] ? "PASS" : "FAIL",
               gate_states_[3] ? "PASS" : "FAIL",
               gate_states_[4] ? "PASS" : "FAIL");
        printf("  Last block: %s\n", block_reason_str(block_reason_));
        printf("  Daily PnL: $%.2f, Trades: %d, Wins: %d, Streak: %d\n",
               daily_pnl_, trades_today_, wins_today_, loss_streak_);
        printf("  Shadow signals: %lu\n", (unsigned long)shadow_signals_);
        
        // Positions
        for (size_t i = 0; i < Constants::NUM_ALLOWED_SYMBOLS; i++) {
            if (positions_[i] != 0.0) {
                printf("  Position [%s]: %.6f @ %.2f class=%s\n",
                       Constants::ALLOWED_SYMBOLS[i], positions_[i], 
                       entry_prices_[i], trade_class_str(trade_classes_[i]));
            }
        }
        printf("\n");
    }
    
    // Get block counts for diagnostics
    uint64_t getBlockCount(BlockReason reason) const noexcept {
        return block_counts_[static_cast<size_t>(reason)];
    }

private:
    // =========================================================================
    // GATE IMPLEMENTATIONS
    // =========================================================================
    
    [[nodiscard]] bool evaluateG1_Infrastructure() noexcept {
        // Check median RTT
        if (latency_tracker_.median() > Constants::MEDIAN_RTT_LIMIT_MS) {
            block_reason_ = BlockReason::LATENCY_HIGH;
            recordBlock(BlockReason::LATENCY_HIGH);
            return false;
        }
        
        // Check P99 RTT
        if (latency_tracker_.p99() > Constants::P99_RTT_LIMIT_MS) {
            block_reason_ = BlockReason::LATENCY_HIGH;
            recordBlock(BlockReason::LATENCY_HIGH);
            return false;
        }
        
        // Check packet loss
        if (latency_tracker_.hasRecentPacketLoss()) {
            block_reason_ = BlockReason::PACKET_LOSS;
            recordBlock(BlockReason::PACKET_LOSS);
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool evaluateG2_MarketQuality(const MarketSnapshot& snap) noexcept {
        // Check spread vs session median
        if (snap.session_median_spread > 0 && 
            snap.spread > snap.session_median_spread * Constants::SPREAD_MULT_LIMIT) {
            block_reason_ = BlockReason::SPREAD_WIDE;
            recordBlock(BlockReason::SPREAD_WIDE);
            return false;
        }
        
        // Check minimum depth (symbol-specific)
        double min_depth = Constants::BTCUSDT_MIN_DEPTH;
        if (strcmp(snap.symbol, "ETHUSDT") == 0) {
            min_depth = Constants::ETHUSDT_MIN_DEPTH;
        }
        
        if (snap.bid_depth < min_depth || snap.ask_depth < min_depth) {
            block_reason_ = BlockReason::DEPTH_LOW;
            recordBlock(BlockReason::DEPTH_LOW);
            return false;
        }
        
        // Check for crossed/locked book
        if (snap.bid >= snap.ask) {
            block_reason_ = BlockReason::BOOK_CROSSED;
            recordBlock(BlockReason::BOOK_CROSSED);
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool evaluateG3_Volatility(const MarketSnapshot& snap) noexcept {
        // Check short-term vol cap
        if (snap.realized_vol_bps > vol_cap_bps_ * Constants::VOL_CAP_MULT) {
            block_reason_ = BlockReason::VOL_HIGH;
            recordBlock(BlockReason::VOL_HIGH);
            return false;
        }
        
        // Check for impulse candle (N sigma move)
        if (std::abs(snap.momentum_bps) > vol_cap_bps_ * Constants::VOL_CAP_MULT) {
            block_reason_ = BlockReason::IMPULSE_DETECTED;
            recordBlock(BlockReason::IMPULSE_DETECTED);
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool evaluateG4_CrossAsset() noexcept {
        if (crypto_stress_ > Constants::CRYPTO_STRESS_THRESHOLD) {
            block_reason_ = BlockReason::CRYPTO_STRESS;
            recordBlock(BlockReason::CRYPTO_STRESS);
            return false;
        }
        
        if (equity_stress_ > Constants::EQUITY_STRESS_THRESHOLD) {
            block_reason_ = BlockReason::EQUITY_STRESS;
            recordBlock(BlockReason::EQUITY_STRESS);
            return false;
        }
        
        if (income_exposure_ > Constants::INCOME_EXPOSURE_LIMIT) {
            block_reason_ = BlockReason::INCOME_EXPOSURE;
            recordBlock(BlockReason::INCOME_EXPOSURE);
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool evaluateG5_Discipline() noexcept {
        // Daily stop loss
        if (daily_pnl_ <= Constants::DAILY_STOP_USD) {
            block_reason_ = BlockReason::DAILY_STOP_HIT;
            recordBlock(BlockReason::DAILY_STOP_HIT);
            return false;
        }
        
        // Loss streak limit
        if (loss_streak_ >= Constants::MAX_LOSS_STREAK) {
            block_reason_ = BlockReason::LOSS_STREAK;
            recordBlock(BlockReason::LOSS_STREAK);
            return false;
        }
        
        // Max trades per session
        if (trades_today_ >= Constants::MAX_TRADES_PER_SESSION) {
            block_reason_ = BlockReason::MAX_TRADES;
            recordBlock(BlockReason::MAX_TRADES);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // TRADE CLASS IMPLEMENTATIONS
    // =========================================================================
    
    [[nodiscard]] TradeSignal evaluateClassA_LiquidityVacuum(const MarketSnapshot& snap) noexcept {
        TradeSignal signal;
        signal.symbol = snap.symbol;
        signal.trade_class = TradeClass::LIQUIDITY_VACUUM;
        
        // Condition 1: Depth dropped >= 70% within 50ms
        if (std::abs(snap.depth_change_pct) < Constants::DEPTH_DROP_THRESHOLD) {
            return signal;  // No vacuum detected
        }
        
        // Condition 2: Spread widened but not gapped (still tradeable)
        // (Already checked in G2)
        
        // Condition 3: Aggressive flow paused >= 20ms
        if (snap.aggressor_pause_ms < Constants::AGGRESSIVE_PAUSE_MS) {
            return signal;  // Flow still active
        }
        
        // All conditions met - generate signal
        signal.valid = true;
        
        // Direction: Follow last aggressive flow
        signal.direction = snap.last_aggressor;
        
        // Entry price (marketable limit)
        if (signal.direction > 0) {
            signal.entry_price = snap.ask;  // Buy at ask
            signal.tp_price = snap.ask + Constants::CLASS_A_TP_TICKS * snap.spread;
            signal.sl_price = snap.ask - Constants::CLASS_A_SL_TICKS * snap.spread;
        } else {
            signal.entry_price = snap.bid;  // Sell at bid
            signal.tp_price = snap.bid - Constants::CLASS_A_TP_TICKS * snap.spread;
            signal.sl_price = snap.bid + Constants::CLASS_A_SL_TICKS * snap.spread;
        }
        
        signal.timeout_ms = Constants::CLASS_A_TIMEOUT_MS;
        snprintf(signal.entry_reason, sizeof(signal.entry_reason),
                 "VACUUM depth_drop=%.0f%% pause=%lums",
                 snap.depth_change_pct * 100.0, (unsigned long)snap.aggressor_pause_ms);
        
        return signal;
    }
    
    [[nodiscard]] TradeSignal evaluateClassB_MomentumContinuation(const MarketSnapshot& snap) noexcept {
        TradeSignal signal;
        signal.symbol = snap.symbol;
        signal.trade_class = TradeClass::MOMENTUM_CONTINUATION;
        
        // Only trade if Class A is inactive
        // (Handled by priority in main evaluate)
        
        // Condition 1: Book imbalance persisted >= 300ms
        // (Would need state tracking - simplified here)
        if (std::abs(snap.imbalance) < 0.3) {
            return signal;  // No strong imbalance
        }
        
        // Condition 2: Trade flow aligns with imbalance
        if ((snap.imbalance > 0 && snap.last_aggressor < 0) ||
            (snap.imbalance < 0 && snap.last_aggressor > 0)) {
            return signal;  // Flow doesn't align
        }
        
        // Condition 3: Spread stable (not widening)
        // (Would compare to recent spreads - simplified here)
        
        // Generate signal
        signal.valid = true;
        signal.direction = snap.imbalance > 0 ? 1 : -1;
        
        if (signal.direction > 0) {
            signal.entry_price = snap.ask;
            signal.tp_price = snap.ask + Constants::CLASS_B_TP_TICKS * snap.spread;
            signal.sl_price = snap.ask - Constants::CLASS_B_SL_TICKS * snap.spread;
        } else {
            signal.entry_price = snap.bid;
            signal.tp_price = snap.bid - Constants::CLASS_B_TP_TICKS * snap.spread;
            signal.sl_price = snap.bid + Constants::CLASS_B_SL_TICKS * snap.spread;
        }
        
        signal.timeout_ms = Constants::CLASS_B_TIMEOUT_MS;
        snprintf(signal.entry_reason, sizeof(signal.entry_reason),
                 "MOMENTUM imb=%.2f flow=%d",
                 snap.imbalance, snap.last_aggressor);
        
        return signal;
    }
    
    // =========================================================================
    // HELPERS
    // =========================================================================
    
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    void disableFor(uint64_t duration_ns) noexcept {
        disabled_until_ns_ = now_ns() + duration_ns;
        state_ = RulesetState::BLOCKED;
    }
    
    double getFixedSize(const char* symbol) const noexcept {
        if (strcmp(symbol, "BTCUSDT") == 0) return Constants::BTCUSDT_SIZE;
        if (strcmp(symbol, "ETHUSDT") == 0) return Constants::ETHUSDT_SIZE;
        return 0.0;
    }
    
    void recordBlock(BlockReason reason) noexcept {
        block_reason_ = reason;
        if (static_cast<size_t>(reason) < block_counts_.size()) {
            block_counts_[static_cast<size_t>(reason)]++;
        }
    }
    
    void logShadowSignal(const TradeSignal& signal) const noexcept {
        printf("[CRYPTO-RULESET-SHADOW] %s %s %s dir=%d entry=%.2f reason=%s\n",
               signal.symbol,
               trade_class_str(signal.trade_class),
               signal.direction > 0 ? "LONG" : "SHORT",
               signal.direction,
               signal.entry_price,
               signal.entry_reason);
    }
    
    // =========================================================================
    // STATE
    // =========================================================================
    
    // Core state
    RulesetState state_ = RulesetState::DISABLED;
    bool enabled_ = false;
    bool shadow_mode_ = true;
    bool shadow_validated_ = false;
    BlockReason block_reason_ = BlockReason::DISABLED;
    
    // Timing
    uint64_t last_trade_ns_ = 0;
    uint64_t disabled_until_ns_ = 0;
    
    // Risk tracking
    double daily_pnl_ = 0.0;
    int loss_streak_ = 0;
    int trades_today_ = 0;
    int wins_today_ = 0;
    
    // Positions (indexed by symbol)
    std::array<double, Constants::NUM_ALLOWED_SYMBOLS> positions_{};
    std::array<double, Constants::NUM_ALLOWED_SYMBOLS> entry_prices_{};
    std::array<uint64_t, Constants::NUM_ALLOWED_SYMBOLS> entry_times_{};
    std::array<TradeClass, Constants::NUM_ALLOWED_SYMBOLS> trade_classes_{};
    
    // Gate states
    std::array<bool, 5> gate_states_{true, true, true, true, true};
    
    // External state (set by coordinator)
    double crypto_stress_ = 0.0;
    double equity_stress_ = 0.0;
    double income_exposure_ = 0.0;
    
    // Latency tracking
    LatencyTracker latency_tracker_;
    
    // Volatility cap (updated from market data)
    double vol_cap_bps_ = 10.0;  // Default conservative
    
    // Statistics
    uint64_t shadow_signals_ = 0;
    uint64_t slippage_kills_ = 0;
    std::array<uint64_t, static_cast<size_t>(BlockReason::MAX_REASON)> block_counts_{};
};

// =============================================================================
// GLOBAL SINGLETON
// =============================================================================
inline CryptoRuleset& getCryptoRuleset() {
    static CryptoRuleset instance;
    return instance;
}

} // namespace Crypto
} // namespace Chimera
