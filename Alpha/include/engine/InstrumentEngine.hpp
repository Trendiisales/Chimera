// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Instrument Engine v1.1.0
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.1.0
// PURPOSE: Complete trading logic for ONE instrument with WINNER-BIASED EXITS
// 
// CORE IDENTITY CHANGE:
// ❌ "Fast in, fast out"
// ✅ "Fast in, patient out"
//
// INTEGRATES:
// - Asia Exception Gate
// - Per-symbol exit profiles
// - Session-specific tolerance
// - News-aware hold extensions
// - Volatility-adaptive patience
// - Auto-disable negative expectancy
// - PnL Attribution logging
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "core/Types.hpp"
#include "session/SessionDetector.hpp"
#include "session/AsiaExceptionGate.hpp"
#include "exit/ExitLogic.hpp"
#include "exit/VolatilityHold.hpp"
#include "exit/NewsGate.hpp"
#include "exit/GoldTrailingEngine.hpp"
#include "exit/GoldPyramidRule.hpp"
#include "risk/ExpectancyGuard.hpp"
#include "risk/PnLAttribution.hpp"
#include "fix/FIXConfig.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <mutex>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// EMA HELPER
// ═══════════════════════════════════════════════════════════════════════════════
class EMA {
public:
    explicit EMA(double alpha = 0.1) noexcept : alpha_(alpha), value_(0), initialized_(false) {}
    
    void update(double v) noexcept {
        if (!initialized_) { value_ = v; initialized_ = true; }
        else { value_ = alpha_ * v + (1.0 - alpha_) * value_; }
    }
    
    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    void reset() noexcept { value_ = 0; initialized_ = false; }
    
private:
    double alpha_, value_;
    bool initialized_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ATR TRACKER (for volatility-adaptive exits)
// ═══════════════════════════════════════════════════════════════════════════════
class ATRTracker {
public:
    void update(double high, double low, double close) noexcept {
        if (!initialized_) {
            prev_close_ = close;
            baseline_atr_ = high - low;
            current_atr_ = baseline_atr_;
            initialized_ = true;
            return;
        }
        
        double tr = std::max({
            high - low,
            std::abs(high - prev_close_),
            std::abs(low - prev_close_)
        });
        
        current_atr_ = 0.1 * tr + 0.9 * current_atr_;
        if (count_ < 100) {
            baseline_atr_ = current_atr_;
        } else {
            baseline_atr_ = 0.001 * current_atr_ + 0.999 * baseline_atr_;
        }
        
        prev_close_ = close;
        ++count_;
    }
    
    [[nodiscard]] double atr_ratio() const noexcept {
        return baseline_atr_ > 0 ? current_atr_ / baseline_atr_ : 1.0;
    }
    
    [[nodiscard]] double current_atr() const noexcept { return current_atr_; }
    [[nodiscard]] double baseline_atr() const noexcept { return baseline_atr_; }
    
private:
    double prev_close_ = 0;
    double current_atr_ = 0;
    double baseline_atr_ = 0;
    bool initialized_ = false;
    int count_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// INSTRUMENT PROFILE (static trading parameters)
// ═══════════════════════════════════════════════════════════════════════════════
struct InstrumentProfile {
    // Entry thresholds
    double min_signal_strength = 0.5;
    double min_edge_bps = 2.0;
    double max_spread = 0.28;
    double min_displacement = 0.35;
    
    // Stop/Target (initial - exits are winner-biased)
    double tp_bps = 10.0;
    double sl_bps = 5.0;
    double trail_activate_bps = 6.0;
    double trail_step_bps = 2.0;
    uint64_t min_hold_ms = 1200;
    
    // Risk
    double base_risk = 0.005;
    double peak_risk = 0.012;
    
    // Regime filters
    bool trade_trending = true;
    bool trade_ranging = true;
    bool trade_volatile = false;
    
    static InstrumentProfile gold() noexcept {
        InstrumentProfile p;
        p.min_signal_strength = 0.55;
        p.min_edge_bps = 1.2;             // From CfdThresholds
        p.max_spread = 0.28;              // From CfdThresholds
        p.min_displacement = 0.35;        // From CfdThresholds
        p.tp_bps = 15.0;
        p.sl_bps = 5.0;
        p.trail_activate_bps = 8.0;
        p.trail_step_bps = 3.0;
        p.min_hold_ms = 2000;             // Gold trends cleaner
        p.base_risk = 0.005;
        p.peak_risk = 0.012;
        p.trade_trending = true;
        p.trade_ranging = true;
        p.trade_volatile = true;
        return p;
    }
    
    static InstrumentProfile nas100() noexcept {
        InstrumentProfile p;
        p.min_signal_strength = 0.6;
        p.min_edge_bps = 1.1;             // From CfdThresholds
        p.max_spread = 1.10;              // From CfdThresholds
        p.min_displacement = 0.30;        // From CfdThresholds
        p.tp_bps = 12.0;
        p.sl_bps = 4.0;
        p.trail_activate_bps = 6.0;
        p.trail_step_bps = 2.0;
        p.min_hold_ms = 1500;
        p.base_risk = 0.004;
        p.peak_risk = 0.010;
        p.trade_trending = true;
        p.trade_ranging = false;          // NAS ranging = chop death
        p.trade_volatile = false;
        return p;
    }
    
    static InstrumentProfile get(Instrument i) noexcept {
        switch (i) {
            case Instrument::XAUUSD: return gold();
            case Instrument::NAS100: return nas100();
            default: return {};
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// MARKET REGIME
// ═══════════════════════════════════════════════════════════════════════════════
enum class Regime : uint8_t { TRENDING, RANGING, VOLATILE, QUIET, TRANSITION };

inline const char* regime_str(Regime r) noexcept {
    switch (r) {
        case Regime::TRENDING: return "TREND";
        case Regime::RANGING: return "RANGE";
        case Regime::VOLATILE: return "VOLATILE";
        case Regime::QUIET: return "QUIET";
        default: return "TRANS";
    }
}

inline bool is_clean_trend(Regime r) noexcept {
    return r == Regime::TRENDING;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SIGNAL
// ═══════════════════════════════════════════════════════════════════════════════
struct Signal {
    Side direction = Side::FLAT;
    double strength = 0.0;
    double edge_bps = 0.0;
    double displacement = 0.0;
    Regime regime = Regime::QUIET;
    const char* reason = "";
    
    [[nodiscard]] bool valid() const noexcept { return direction != Side::FLAT && strength > 0; }
};

// ═══════════════════════════════════════════════════════════════════════════════
// INSTRUMENT ENGINE - Complete trading logic with WINNER-BIASED EXITS
// ═══════════════════════════════════════════════════════════════════════════════
class InstrumentEngine {
public:
    explicit InstrumentEngine(Instrument inst, double equity = 10000.0) noexcept
        : instrument_(inst)
        , profile_(InstrumentProfile::get(inst))
        , spec_(get_spec(inst))
        , equity_(equity)
        , state_(EngineState::INIT)
        , momentum_fast_(0.3)
        , momentum_slow_(0.1)
        , volatility_(0.05)
        , spread_ema_(0.1)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // LIFECYCLE
    // ─────────────────────────────────────────────────────────────────────────
    void start() noexcept {
        state_ = EngineState::WARMUP;
        tick_count_ = 0;
        last_mid_ = 0;
        regime_ = Regime::TRANSITION;
        position_state_ = {};
        
        std::cout << "[" << instrument_str(instrument_) << "] Engine started (WARMUP)\n";
    }
    
    void stop() noexcept {
        state_ = EngineState::SHUTDOWN;
        std::cout << "[" << instrument_str(instrument_) << "] Engine stopped\n";
    }
    
    void set_equity(double eq) noexcept { equity_ = eq; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // TICK PROCESSING - Main entry point
    // ─────────────────────────────────────────────────────────────────────────
    void on_tick(const Tick& tick) noexcept {
        if (!tick.valid() || tick.instrument != instrument_) return;
        
        ++tick_count_;
        latest_tick_ = tick;
        
        // Update microstructure
        update_microstructure(tick);
        
        // Warmup check
        if (state_ == EngineState::WARMUP && tick_count_ >= 100) {
            state_ = EngineState::RUNNING;
            std::cout << "[" << instrument_str(instrument_) << "] Warmup complete -> RUNNING\n";
        }
        
        // Only trade if RUNNING
        if (state_ != EngineState::RUNNING) return;
        
        // ═══════════════════════════════════════════════════════════════════════
        // EXPECTANCY GUARD: Auto-disable if negative expectancy
        // ═══════════════════════════════════════════════════════════════════════
        if (!symbol_enabled(instrument_str(instrument_))) {
            return;  // Engine fired itself
        }
        
        // Check for exit if we have position
        if (position_state_.side != 0) {
            check_exit_winner_biased(tick);
            return;
        }
        
        // Look for entry
        check_entry(tick);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // ORDER CALLBACK (from FIX client)
    // ─────────────────────────────────────────────────────────────────────────
    using OrderCallback = std::function<bool(const OrderIntent&)>;
    void set_order_callback(OrderCallback cb) noexcept { order_callback_ = std::move(cb); }
    
    // ─────────────────────────────────────────────────────────────────────────
    // GETTERS
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] Instrument instrument() const noexcept { return instrument_; }
    [[nodiscard]] EngineState state() const noexcept { return state_; }
    [[nodiscard]] Regime regime() const noexcept { return regime_; }
    [[nodiscard]] uint64_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] double spread_bps() const noexcept { return spread_ema_.value(); }
    [[nodiscard]] double atr_ratio() const noexcept { return atr_.atr_ratio(); }
    [[nodiscard]] bool has_position() const noexcept { return position_state_.side != 0; }
    [[nodiscard]] double current_r() const noexcept { return position_state_.open_r; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // FILL NOTIFICATION (from FIX execution reports)
    // ─────────────────────────────────────────────────────────────────────────
    void on_fill(Side side, double price, double qty, bool is_close) noexcept {
        if (is_close && position_state_.side != 0) {
            // Calculate final PnL
            double pnl_bps = 0;
            if (position_state_.side > 0) {
                pnl_bps = (price - position_state_.entry) / position_state_.entry * 10000.0;
            } else {
                pnl_bps = (position_state_.entry - price) / position_state_.entry * 10000.0;
            }
            
            uint64_t hold_ms = (now_ns() - position_state_.entry_ns) / 1'000'000ULL;
            
            // ═══════════════════════════════════════════════════════════════════
            // PNL ATTRIBUTION LOGGING
            // ═══════════════════════════════════════════════════════════════════
            PnLRecord rec;
            rec.timestamp_ns = now_ns();
            rec.symbol = instrument_str(instrument_);
            rec.side = position_state_.side;
            rec.entry_price = position_state_.entry;
            rec.exit_price = price;
            rec.size = qty;
            rec.r_multiple = position_state_.open_r;
            rec.pnl_bps = pnl_bps;
            rec.session = position_state_.entry_session;
            rec.regime = regime_str(regime_);
            rec.atr_ratio = atr_.atr_ratio();
            rec.spread_at_entry = position_state_.entry_spread;
            rec.edge_at_entry = position_state_.entry_edge;
            rec.hold_ms = hold_ms;
            rec.scaled = position_state_.scaled;
            rec.moved_to_breakeven = position_state_.risk_free;
            rec.exit_reason = last_exit_reason_;
            
            log_pnl(rec);
            
            // Record for expectancy tracking
            record_trade(instrument_str(instrument_), position_state_.open_r, hold_ms, position_state_.scaled);
            
            std::cout << "[" << instrument_str(instrument_) << "] CLOSED "
                      << (position_state_.side > 0 ? "LONG" : "SHORT") 
                      << " R=" << std::fixed << std::setprecision(2) << position_state_.open_r
                      << " PnL=" << pnl_bps << "bps"
                      << " Hold=" << hold_ms << "ms"
                      << " Exit=" << last_exit_reason_ << "\n";
            
            // Reset position
            position_state_ = {};
            stop_move_count_ = 0;
            
            // Cooldown
            cooldown_until_ = now_ms() + (pnl_bps > 0 ? 10000 : 30000);
            if (pnl_bps < 0) ++daily_losses_;
            
        } else if (!is_close) {
            // New position
            position_state_.symbol = instrument_str(instrument_);
            position_state_.side = (side == Side::LONG) ? 1 : -1;
            position_state_.entry = price;
            position_state_.stop = (side == Side::LONG) 
                ? price * (1.0 - profile_.sl_bps / 10000.0)
                : price * (1.0 + profile_.sl_bps / 10000.0);
            position_state_.entry_ns = now_ns();
            position_state_.entry_edge = last_signal_edge_;
            position_state_.entry_spread = latest_tick_.spread;
            position_state_.entry_session = current_session_type();
            position_state_.open_r = 0.0;
            position_state_.risk_free = false;
            position_state_.scaled = false;
            
            highest_price_ = price;
            lowest_price_ = price;
            stop_move_count_ = 0;  // Reset for Gold structure-based trailing
            
            std::cout << "[" << instrument_str(instrument_) << "] OPENED "
                      << side_str(side) << " " << std::fixed << std::setprecision(2)
                      << qty << " @ " << price 
                      << " Session=" << session_type_str(position_state_.entry_session) << "\n";
        }
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // MICROSTRUCTURE ANALYSIS
    // ─────────────────────────────────────────────────────────────────────────
    void update_microstructure(const Tick& tick) noexcept {
        if (last_mid_ > 0) {
            double delta_bps = (tick.mid - last_mid_) / last_mid_ * 10000.0;
            momentum_fast_.update(delta_bps);
            momentum_slow_.update(delta_bps);
            volatility_.update(std::abs(delta_bps));
        }
        last_mid_ = tick.mid;
        spread_ema_.update(tick.spread_bps);
        
        // Update ATR
        atr_.update(tick.mid * 1.0001, tick.mid * 0.9999, tick.mid);  // Approximation
        
        // Update regime
        update_regime();
    }
    
    void update_regime() noexcept {
        if (tick_count_ < 30) { regime_ = Regime::TRANSITION; return; }
        
        double mom_fast = std::abs(momentum_fast_.value());
        double mom_slow = std::abs(momentum_slow_.value());
        double vol = volatility_.value();
        
        Regime new_regime = Regime::QUIET;
        if (vol > 2.5) new_regime = Regime::VOLATILE;
        else if (mom_fast > 0.8 && mom_slow > 0.5) new_regime = Regime::TRENDING;
        else if (vol < 0.5) new_regime = Regime::QUIET;
        else if (mom_fast < 0.4) new_regime = Regime::RANGING;
        else new_regime = Regime::TRANSITION;
        
        if (new_regime != regime_) {
            if (++regime_ticks_ >= 10) {
                regime_ = new_regime;
                regime_ticks_ = 0;
            }
        } else {
            regime_ticks_ = 0;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // SIGNAL GENERATION
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] Signal generate_signal() const noexcept {
        Signal sig;
        sig.regime = regime_;
        
        if (tick_count_ < 50) { sig.reason = "WARMUP"; return sig; }
        
        double mom_fast = momentum_fast_.value();
        double mom_slow = momentum_slow_.value();
        
        // Calculate displacement
        sig.displacement = std::abs(mom_fast);
        
        switch (regime_) {
            case Regime::TRENDING:
                if (mom_fast > 0 && mom_slow > 0) {
                    sig.direction = Side::LONG;
                    sig.strength = std::min(1.0, (mom_fast + mom_slow) / 3.0);
                    sig.edge_bps = std::min(mom_fast, mom_slow) * 0.7;
                    sig.reason = "TREND_LONG";
                } else if (mom_fast < 0 && mom_slow < 0) {
                    sig.direction = Side::SHORT;
                    sig.strength = std::min(1.0, (std::abs(mom_fast) + std::abs(mom_slow)) / 3.0);
                    sig.edge_bps = std::min(std::abs(mom_fast), std::abs(mom_slow)) * 0.7;
                    sig.reason = "TREND_SHORT";
                }
                break;
                
            case Regime::RANGING:
                if (mom_fast > 1.5 && mom_slow > 0) {
                    sig.direction = Side::SHORT;
                    sig.strength = std::min(1.0, (mom_fast - 1.5) / 2.0);
                    sig.edge_bps = (mom_fast - 1.5) * 0.5;
                    sig.reason = "RANGE_FADE_UP";
                } else if (mom_fast < -1.5 && mom_slow < 0) {
                    sig.direction = Side::LONG;
                    sig.strength = std::min(1.0, (std::abs(mom_fast) - 1.5) / 2.0);
                    sig.edge_bps = (std::abs(mom_fast) - 1.5) * 0.5;
                    sig.reason = "RANGE_FADE_DOWN";
                }
                break;
                
            case Regime::VOLATILE:
                if (mom_fast > 2.5) {
                    sig.direction = Side::LONG;
                    sig.strength = std::min(1.0, mom_fast / 5.0) * 0.6;
                    sig.edge_bps = mom_fast * 0.4;
                    sig.reason = "VOL_BURST_LONG";
                } else if (mom_fast < -2.5) {
                    sig.direction = Side::SHORT;
                    sig.strength = std::min(1.0, std::abs(mom_fast) / 5.0) * 0.6;
                    sig.edge_bps = std::abs(mom_fast) * 0.4;
                    sig.reason = "VOL_BURST_SHORT";
                }
                break;
                
            default:
                sig.reason = "REGIME_BLOCKED";
                break;
        }
        
        return sig;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // ENTRY CHECK (with Asia Exception Gate)
    // ─────────────────────────────────────────────────────────────────────────
    void check_entry(const Tick& tick) noexcept {
        // Session check
        auto session = current_session(instrument_);
        SessionType sess_type = current_session_type();
        
        // ═══════════════════════════════════════════════════════════════════════
        // ASIA EXCEPTION GATE
        // ═══════════════════════════════════════════════════════════════════════
        if (sess_type == SessionType::ASIA) {
            // Build extraordinary signal check
            AsiaExtraordinarySignal asia_sig {
                .edge = last_signal_edge_,
                .displacement = std::abs(momentum_fast_.value()),
                .atr_ratio = atr_.atr_ratio(),
                .spread = tick.spread,
                .clean_regime = is_clean_trend(regime_),
                .no_news = !high_impact_news_near(std::time(nullptr))
            };
            
            if (!allow_asia_exception(instrument_str(instrument_), asia_sig)) {
                log_asia_block(instrument_str(instrument_), "NORMAL");
                return;  // Block Asia trading
            }
            
            log_asia_allow(instrument_str(instrument_));
            // Asia exception - use reduced risk
            session.size_multiplier = ASIA_RISK_MULTIPLIER;
        }
        
        if (!session.can_trade()) return;
        
        // ═══════════════════════════════════════════════════════════════════════
        // NEWS GATE: Block entries during high-impact news
        // ═══════════════════════════════════════════════════════════════════════
        if (news_blocks_entry(std::time(nullptr))) {
            return;
        }
        
        // Cooldown check
        if (now_ms() < cooldown_until_) return;
        
        // Daily limit check
        if (daily_trades_ >= 20) return;
        if (daily_losses_ >= 5) return;
        
        // Signal generation
        Signal signal = generate_signal();
        if (!signal.valid()) return;
        
        // ═══════════════════════════════════════════════════════════════════════
        // CfdThresholds GATES (hard numerical gates - no vibes)
        // ═══════════════════════════════════════════════════════════════════════
        if (signal.strength < profile_.min_signal_strength) return;
        if (signal.edge_bps < profile_.min_edge_bps) return;
        if (signal.displacement < profile_.min_displacement) return;
        if (tick.spread > profile_.max_spread) return;
        
        last_signal_edge_ = signal.edge_bps;
        
        // Regime check
        bool regime_ok = false;
        switch (signal.regime) {
            case Regime::TRENDING: regime_ok = profile_.trade_trending; break;
            case Regime::RANGING: regime_ok = profile_.trade_ranging; break;
            case Regime::VOLATILE: regime_ok = profile_.trade_volatile; break;
            default: break;
        }
        if (!regime_ok) return;
        
        // Calculate position size
        double risk_frac = profile_.base_risk;
        if (session.is_peak) risk_frac = profile_.peak_risk * session.size_multiplier / 2.0;
        
        // Asia gets reduced risk
        if (sess_type == SessionType::ASIA) {
            risk_frac *= ASIA_RISK_MULTIPLIER;
        }
        
        risk_frac = std::clamp(risk_frac, 0.002, 0.015);
        
        double risk_amount = equity_ * risk_frac;
        double stop_distance = profile_.sl_bps / 10000.0 * tick.mid;
        double size = risk_amount / (stop_distance * 100.0);
        size = std::floor(size / spec_.lot_step) * spec_.lot_step;
        size = std::clamp(size, spec_.min_lot, spec_.max_lots);
        
        if (size < spec_.min_lot) return;
        
        // Build order
        OrderIntent order;
        order.instrument = instrument_;
        order.side = signal.direction;
        order.size = size;
        order.is_close = false;
        
        if (signal.direction == Side::LONG) {
            order.entry_price = tick.ask;
            order.stop_loss = order.entry_price * (1.0 - profile_.sl_bps / 10000.0);
            order.take_profit = order.entry_price * (1.0 + profile_.tp_bps / 10000.0);
        } else {
            order.entry_price = tick.bid;
            order.stop_loss = order.entry_price * (1.0 + profile_.sl_bps / 10000.0);
            order.take_profit = order.entry_price * (1.0 - profile_.tp_bps / 10000.0);
        }
        
        std::cout << "\n[" << instrument_str(instrument_) << "] SIGNAL: "
                  << side_str(signal.direction) << " str=" << std::fixed << std::setprecision(2)
                  << signal.strength << " edge=" << signal.edge_bps << "bps"
                  << " disp=" << signal.displacement
                  << " session=" << session_str(session.session)
                  << " regime=" << regime_str(signal.regime) << "\n";
        
        // Send order
        if (order_callback_ && order_callback_(order)) {
            ++daily_trades_;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // EXIT CHECK - WINNER-BIASED (THE KEY LOGIC)
    // ─────────────────────────────────────────────────────────────────────────
    void check_exit_winner_biased(const Tick& tick) noexcept {
        // Update position R-multiple
        double current_price = tick.mid;
        if (position_state_.side > 0) {
            position_state_.open_r = (current_price - position_state_.entry) / position_state_.entry * 10000.0 / profile_.sl_bps;
            highest_price_ = std::max(highest_price_, current_price);
        } else {
            position_state_.open_r = (position_state_.entry - current_price) / position_state_.entry * 10000.0 / profile_.sl_bps;
            lowest_price_ = std::min(lowest_price_, current_price);
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // MOVE TO BREAKEVEN (if threshold met and not already done)
        // ═══════════════════════════════════════════════════════════════════════
        if (should_move_to_breakeven(position_state_) && !position_state_.risk_free) {
            position_state_.stop = position_state_.entry;
            position_state_.risk_free = true;
            std::cout << "[" << instrument_str(instrument_) << "] Moved to BREAKEVEN at R=" 
                      << std::fixed << std::setprecision(2) << position_state_.open_r << "\n";
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // SCALE CHECK (if conditions met and not during Asia/news)
        // ═══════════════════════════════════════════════════════════════════════
        if (should_scale(position_state_) && !position_state_.scaled) {
            // NOTE: Scaling requires external callback - just mark intent here
            position_state_.scaled = true;  // Would trigger scale order
            std::cout << "[" << instrument_str(instrument_) << "] SCALE opportunity at R="
                      << std::fixed << std::setprecision(2) << position_state_.open_r << "\n";
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // WINNER-BIASED EXIT EVALUATION
        // ═══════════════════════════════════════════════════════════════════════
        double edge = momentum_fast_.value();  // Current edge estimate
        double atr_ratio = atr_.atr_ratio();
        SessionType session = current_session_type();
        
        ExitDecision decision = evaluate_exit(
            position_state_,
            edge,
            atr_ratio,
            session,
            now_ns(),
            std::time(nullptr)
        );
        
        bool should_exit = decision.should_exit;
        last_exit_reason_ = decision.reason;
        
        // ═══════════════════════════════════════════════════════════════════════
        // HARD STOP LOSS (never violated)
        // ═══════════════════════════════════════════════════════════════════════
        if (position_state_.open_r <= -1.0) {
            should_exit = true;
            last_exit_reason_ = "STOP_LOSS";
            ++daily_losses_;
        }
        
        // ═══════════════════════════════════════════════════════════════════════════
        // GOLD STRUCTURE-BASED TRAILING (for XAUUSD only)
        // ═══════════════════════════════════════════════════════════════════════════
        if (instrument_ == Instrument::XAUUSD) {
            GoldTrailUpdate trail = gold_trail(
                position_state_,
                highest_price_,      // expansion_high
                lowest_price_,       // expansion_low
                atr_.current_atr(),  // atr
                atr_ratio,           // atr_ratio
                stop_move_count_     // current moves
            );
            
            if (trail.move && trail.new_stop != position_state_.stop) {
                position_state_.stop = trail.new_stop;
                stop_move_count_ = trail.stop_move_count;
                std::cout << "[XAUUSD] STOP MOVE #" << stop_move_count_ 
                          << ": " << trail.reason 
                          << " new_stop=" << std::fixed << std::setprecision(2) << trail.new_stop << "\n";
            }
            
            // Check if price hit structural stop
            if (position_state_.side > 0 && tick.bid <= position_state_.stop) {
                should_exit = true;
                last_exit_reason_ = "STRUCTURAL_STOP";
            } else if (position_state_.side < 0 && tick.ask >= position_state_.stop) {
                should_exit = true;
                last_exit_reason_ = "STRUCTURAL_STOP";
            }
            
            // ═══════════════════════════════════════════════════════════════════════
            // GOLD PYRAMID CHECK
            // ═══════════════════════════════════════════════════════════════════════
            if (!position_state_.scaled) {
                GoldPyramidDecision pyramid = check_gold_pyramid(
                    position_state_,
                    atr_ratio,
                    highest_price_,
                    lowest_price_,
                    tick.mid
                );
                
                if (pyramid.allowed) {
                    position_state_.scaled = true;
                    std::cout << "[XAUUSD] PYRAMID TRIGGERED: " << pyramid.reason
                              << " add_size=" << (pyramid.add_size_fraction * 100) << "%"
                              << " R=" << std::fixed << std::setprecision(2) << position_state_.open_r << "\n";
                    // TODO: Send pyramid order via callback
                }
            }
        } else {
            // NAS100: Standard trailing (tighter, tick-based)
            if (position_state_.open_r >= profile_.trail_activate_bps / profile_.sl_bps) {
                double trail_r;
                if (position_state_.side > 0) {
                    double max_r = (highest_price_ - position_state_.entry) / position_state_.entry * 10000.0 / profile_.sl_bps;
                    trail_r = max_r - profile_.trail_step_bps / profile_.sl_bps;
                } else {
                    double max_r = (position_state_.entry - lowest_price_) / position_state_.entry * 10000.0 / profile_.sl_bps;
                    trail_r = max_r - profile_.trail_step_bps / profile_.sl_bps;
                }
                
                if (position_state_.open_r <= trail_r) {
                    should_exit = true;
                    last_exit_reason_ = "TRAILING_STOP";
                }
            }
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // EXECUTE EXIT
        // ═══════════════════════════════════════════════════════════════════════
        if (should_exit && order_callback_) {
            OrderIntent close_order;
            close_order.instrument = instrument_;
            close_order.side = (position_state_.side > 0) ? Side::SHORT : Side::LONG;
            close_order.size = 0;  // Close full position
            close_order.is_close = true;
            close_order.entry_price = (position_state_.side > 0) ? tick.bid : tick.ask;
            
            std::cout << "[" << instrument_str(instrument_) << "] EXIT SIGNAL: "
                      << last_exit_reason_ << " R=" << std::fixed << std::setprecision(2)
                      << position_state_.open_r << "\n";
            
            order_callback_(close_order);
        }
    }
    
    // Member variables
    Instrument instrument_;
    InstrumentProfile profile_;
    InstrumentSpec spec_;
    double equity_;
    EngineState state_;
    
    // Market data
    Tick latest_tick_;
    uint64_t tick_count_ = 0;
    double last_mid_ = 0;
    
    // Microstructure
    EMA momentum_fast_;
    EMA momentum_slow_;
    EMA volatility_;
    EMA spread_ema_;
    ATRTracker atr_;
    Regime regime_ = Regime::QUIET;
    int regime_ticks_ = 0;
    
    // Position (winner-biased state)
    PositionState position_state_;
    double highest_price_ = 0;
    double lowest_price_ = 999999;
    double last_signal_edge_ = 0;
    std::string last_exit_reason_;
    int stop_move_count_ = 0;  // For Gold structure-based trailing (max 3)
    
    // Risk
    uint64_t cooldown_until_ = 0;
    int daily_trades_ = 0;
    int daily_losses_ = 0;
    
    // Callbacks
    OrderCallback order_callback_;
};

}  // namespace Alpha
