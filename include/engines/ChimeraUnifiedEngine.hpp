// =============================================================================
// ChimeraUnifiedEngine.hpp - Chimera v4.14.0 Unified Trading Engine
// =============================================================================
// SYMBOLS: XAUUSD, NAS100, US30 ONLY
// ARCHITECTURE: Clean CRTP-based engines with zero-overhead polymorphism
// 
// NO CRYPTO - NO FOREX - THREE SYMBOLS ONLY
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <functional>
#include <chrono>

namespace chimera {

// =============================================================================
// CORE TYPES
// =============================================================================

enum class Regime : uint8_t {
    DEAD = 0,
    STRUCTURAL_EXPANSION = 1,
    INVENTORY_CORRECTION = 2,
    STOP_SWEEP = 3,
    RANGE_VOLATILE = 4
};

inline const char* regimeStr(Regime r) {
    switch (r) {
        case Regime::DEAD:                  return "DEAD";
        case Regime::STRUCTURAL_EXPANSION:  return "EXPANSION";
        case Regime::INVENTORY_CORRECTION:  return "INV_CORR";
        case Regime::STOP_SWEEP:            return "STOP_SWEEP";
        case Regime::RANGE_VOLATILE:        return "RANGE_VOL";
        default:                            return "UNKNOWN";
    }
}

struct Tick {
    uint64_t ts_ns;
    double   price;
    double   bid;
    double   ask;
    double   volume;
    Regime   regime;
};

struct TradeSignal {
    const char* engine;
    const char* symbol;
    int         direction;   // +1 long, -1 short
    double      size_mult;
    double      price;
    const char* reason;
    uint64_t    ts_ns;
};

// =============================================================================
// CRTP ENGINE BASE
// =============================================================================

template<typename Derived>
class EngineCRTP {
public:
    inline void on_tick(const Tick& t) {
        static_cast<Derived*>(this)->on_tick_impl(t);
    }
    
    inline void reset() {
        static_cast<Derived*>(this)->reset_impl();
    }
};

// =============================================================================
// TIME HELPERS (UTC)
// =============================================================================

static inline uint64_t ns_to_ms(uint64_t ns) {
    return ns / 1'000'000ULL;
}

static inline uint64_t ns_to_minutes(uint64_t ns) {
    return ns / 60'000'000'000ULL;
}

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// =============================================================================
// SESSION GATES
// =============================================================================

// NY Session: 13:30 – 20:00 UTC (9:30 AM - 4:00 PM ET)
static inline bool is_ny_session(uint64_t ts_ns) {
    uint64_t mins = ns_to_minutes(ts_ns);
    uint64_t hour = (mins / 60) % 24;
    uint64_t min = mins % 60;
    
    // Start: 13:30 UTC, End: 20:00 UTC
    if (hour == 13 && min >= 30) return true;
    if (hour > 13 && hour < 20) return true;
    return false;
}

// London Session: 08:00 – 16:00 UTC
static inline bool is_london_session(uint64_t ts_ns) {
    uint64_t mins = ns_to_minutes(ts_ns);
    uint64_t hour = (mins / 60) % 24;
    return (hour >= 8 && hour < 16);
}

// Gold optimal: London + NY overlap (13:30 - 16:00 UTC)
static inline bool is_gold_optimal_session(uint64_t ts_ns) {
    uint64_t mins = ns_to_minutes(ts_ns);
    uint64_t hour = (mins / 60) % 24;
    uint64_t min = mins % 60;
    
    if (hour == 13 && min >= 30) return true;
    if (hour > 13 && hour < 16) return true;
    return false;
}

// =============================================================================
// SIGNAL CALLBACK TYPE
// =============================================================================

using SignalCallback = std::function<void(const TradeSignal&)>;

// =============================================================================
// NAS100 ENGINE 1: Opening Range Gate
// =============================================================================

class NAS100OpeningRangeGate final : public EngineCRTP<NAS100OpeningRangeGate> {
public:
    // === BACKTEST-LOCKED SETTINGS ===
    static constexpr uint64_t OR_WINDOW_MIN = 15;
    static constexpr double   OR_MAX_RANGE  = 120.0;  // points
    static constexpr double   OR_MIN_RANGE  = 25.0;

    std::atomic<bool>   range_valid{false};
    std::atomic<double> or_high{0.0};
    std::atomic<double> or_low{0.0};
    std::atomic<bool>   complete{false};
    std::atomic<uint64_t> session_start{0};

    void on_tick_impl(const Tick& t) {
        if (!is_ny_session(t.ts_ns)) {
            // Reset at session end
            if (complete.load()) {
                reset_impl();
            }
            return;
        }

        uint64_t minute = ns_to_minutes(t.ts_ns);
        
        // Track session start
        if (session_start.load() == 0) {
            session_start.store(minute);
        }
        
        uint64_t session_min = minute - session_start.load();

        // Opening range window: first 15 NY minutes
        if (session_min <= OR_WINDOW_MIN) {
            if (!complete.load(std::memory_order_relaxed)) {
                double hi = or_high.load();
                double lo = or_low.load();

                if (hi == 0.0 || t.price > hi) or_high.store(t.price);
                if (lo == 0.0 || t.price < lo) or_low.store(t.price);
            }
            return;
        }

        if (!complete.exchange(true)) {
            double range = or_high.load() - or_low.load();
            bool valid = (range >= OR_MIN_RANGE && range <= OR_MAX_RANGE);
            range_valid.store(valid);

            std::printf("[NAS100][OR] range=%.2f valid=%s high=%.2f low=%.2f\n",
                   range, valid ? "YES" : "NO", or_high.load(), or_low.load());
        }
    }
    
    void reset_impl() {
        range_valid.store(false);
        or_high.store(0.0);
        or_low.store(0.0);
        complete.store(false);
        session_start.store(0);
    }
};

// =============================================================================
// NAS100 ENGINE 2: Liquidity Sweep Reversion
// =============================================================================

class NAS100SweepReversion final : public EngineCRTP<NAS100SweepReversion> {
public:
    // === BACKTEST-LOCKED SETTINGS ===
    static constexpr double SWEEP_DIST  = 35.0;
    static constexpr double REVERT_DIST = 18.0;
    static constexpr double SIZE_MULT   = 2.0;

    explicit NAS100SweepReversion(NAS100OpeningRangeGate& gate_ref, SignalCallback cb = nullptr)
        : gate_(gate_ref), callback_(cb) {}

    void setCallback(SignalCallback cb) { callback_ = cb; }

    void on_tick_impl(const Tick& t) {
        if (!is_ny_session(t.ts_ns))
            return;

        if (!gate_.complete.load() || !gate_.range_valid.load())
            return;

        double hi = gate_.or_high.load();
        double lo = gate_.or_low.load();

        // Detect sweep
        if (t.price > hi + SWEEP_DIST) {
            sweep_up_.store(true);
            return;
        }
        if (t.price < lo - SWEEP_DIST) {
            sweep_dn_.store(true);
            return;
        }

        // Reversion entry
        if (sweep_up_.load() && t.price < hi + REVERT_DIST) {
            emit(-1, t.price, "sweep_revert_short", t.ts_ns);
            sweep_up_.store(false);
        }

        if (sweep_dn_.load() && t.price > lo - REVERT_DIST) {
            emit(+1, t.price, "sweep_revert_long", t.ts_ns);
            sweep_dn_.store(false);
        }
    }
    
    void reset_impl() {
        sweep_up_.store(false);
        sweep_dn_.store(false);
    }

private:
    NAS100OpeningRangeGate& gate_;
    SignalCallback callback_;
    std::atomic<bool> sweep_up_{false};
    std::atomic<bool> sweep_dn_{false};

    void emit(int dir, double px, const char* reason, uint64_t ts) {
        TradeSignal s{
            "NAS100_SWEEP_REVERSION",
            "NAS100",
            dir,
            SIZE_MULT,
            px,
            reason,
            ts
        };

        std::printf("[SIGNAL][NAS100] %s dir=%d size=%.2f px=%.2f\n",
               s.reason, s.direction, s.size_mult, px);
        
        if (callback_) callback_(s);
    }
};

// =============================================================================
// US30 ENGINE 1: Opening Range Gate (Similar to NAS100)
// =============================================================================

class US30OpeningRangeGate final : public EngineCRTP<US30OpeningRangeGate> {
public:
    // === BACKTEST-LOCKED SETTINGS (US30 specific) ===
    static constexpr uint64_t OR_WINDOW_MIN = 15;
    static constexpr double   OR_MAX_RANGE  = 200.0;  // US30 has larger range
    static constexpr double   OR_MIN_RANGE  = 40.0;

    std::atomic<bool>   range_valid{false};
    std::atomic<double> or_high{0.0};
    std::atomic<double> or_low{0.0};
    std::atomic<bool>   complete{false};
    std::atomic<uint64_t> session_start{0};

    void on_tick_impl(const Tick& t) {
        if (!is_ny_session(t.ts_ns)) {
            if (complete.load()) {
                reset_impl();
            }
            return;
        }

        uint64_t minute = ns_to_minutes(t.ts_ns);
        
        if (session_start.load() == 0) {
            session_start.store(minute);
        }
        
        uint64_t session_min = minute - session_start.load();

        if (session_min <= OR_WINDOW_MIN) {
            if (!complete.load(std::memory_order_relaxed)) {
                double hi = or_high.load();
                double lo = or_low.load();

                if (hi == 0.0 || t.price > hi) or_high.store(t.price);
                if (lo == 0.0 || t.price < lo) or_low.store(t.price);
            }
            return;
        }

        if (!complete.exchange(true)) {
            double range = or_high.load() - or_low.load();
            bool valid = (range >= OR_MIN_RANGE && range <= OR_MAX_RANGE);
            range_valid.store(valid);

            std::printf("[US30][OR] range=%.2f valid=%s high=%.2f low=%.2f\n",
                   range, valid ? "YES" : "NO", or_high.load(), or_low.load());
        }
    }
    
    void reset_impl() {
        range_valid.store(false);
        or_high.store(0.0);
        or_low.store(0.0);
        complete.store(false);
        session_start.store(0);
    }
};

// =============================================================================
// US30 ENGINE 2: Liquidity Sweep Reversion
// =============================================================================

class US30SweepReversion final : public EngineCRTP<US30SweepReversion> {
public:
    // === BACKTEST-LOCKED SETTINGS (US30 specific) ===
    static constexpr double SWEEP_DIST  = 60.0;   // US30 has larger sweeps
    static constexpr double REVERT_DIST = 30.0;
    static constexpr double SIZE_MULT   = 1.5;

    explicit US30SweepReversion(US30OpeningRangeGate& gate_ref, SignalCallback cb = nullptr)
        : gate_(gate_ref), callback_(cb) {}

    void setCallback(SignalCallback cb) { callback_ = cb; }

    void on_tick_impl(const Tick& t) {
        if (!is_ny_session(t.ts_ns))
            return;

        if (!gate_.complete.load() || !gate_.range_valid.load())
            return;

        double hi = gate_.or_high.load();
        double lo = gate_.or_low.load();

        if (t.price > hi + SWEEP_DIST) {
            sweep_up_.store(true);
            return;
        }
        if (t.price < lo - SWEEP_DIST) {
            sweep_dn_.store(true);
            return;
        }

        if (sweep_up_.load() && t.price < hi + REVERT_DIST) {
            emit(-1, t.price, "sweep_revert_short", t.ts_ns);
            sweep_up_.store(false);
        }

        if (sweep_dn_.load() && t.price > lo - REVERT_DIST) {
            emit(+1, t.price, "sweep_revert_long", t.ts_ns);
            sweep_dn_.store(false);
        }
    }
    
    void reset_impl() {
        sweep_up_.store(false);
        sweep_dn_.store(false);
    }

private:
    US30OpeningRangeGate& gate_;
    SignalCallback callback_;
    std::atomic<bool> sweep_up_{false};
    std::atomic<bool> sweep_dn_{false};

    void emit(int dir, double px, const char* reason, uint64_t ts) {
        TradeSignal s{
            "US30_SWEEP_REVERSION",
            "US30",
            dir,
            SIZE_MULT,
            px,
            reason,
            ts
        };

        std::printf("[SIGNAL][US30] %s dir=%d size=%.2f px=%.2f\n",
               s.reason, s.direction, s.size_mult, px);
        
        if (callback_) callback_(s);
    }
};

// =============================================================================
// XAUUSD ENGINE 1: Mean Revert (Inventory Correction)
// =============================================================================

class XAUUSDMeanRevert final : public EngineCRTP<XAUUSDMeanRevert> {
public:
    // === BACKTEST-LOCKED SETTINGS ===
    static constexpr double   VEL_THRESHOLD  = 0.25;   // $/tick velocity
    static constexpr uint64_t COOLDOWN_MS    = 3000;
    static constexpr double   SIZE_MULT      = 0.75;

    explicit XAUUSDMeanRevert(SignalCallback cb = nullptr) : callback_(cb) {}
    void setCallback(SignalCallback cb) { callback_ = cb; }

    void on_tick_impl(const Tick& t) {
        if (!is_gold_optimal_session(t.ts_ns))
            return;
            
        // Only trade in inventory correction regime
        if (t.regime != Regime::INVENTORY_CORRECTION) {
            last_price_ = t.price;
            last_ts_ = ns_to_ms(t.ts_ns);
            return;
        }

        uint64_t now_ms = ns_to_ms(t.ts_ns);
        
        // Cooldown check
        if (now_ms - last_signal_ts_ < COOLDOWN_MS) {
            return;
        }

        // Calculate velocity
        double dt = (now_ms - last_ts_) / 1000.0;
        if (dt <= 0 || last_ts_ == 0) {
            last_price_ = t.price;
            last_ts_ = now_ms;
            return;
        }

        double velocity = (t.price - last_price_) / dt;
        last_price_ = t.price;
        last_ts_ = now_ms;

        // Fade velocity spike
        if (std::fabs(velocity) >= VEL_THRESHOLD) {
            int dir = (velocity > 0) ? -1 : +1;
            emit(dir, t.price, "mean_revert_fade", t.ts_ns);
            last_signal_ts_ = now_ms;
        }
    }
    
    void reset_impl() {
        last_price_ = 0.0;
        last_ts_ = 0;
        last_signal_ts_ = 0;
    }

private:
    SignalCallback callback_;
    double last_price_{0.0};
    uint64_t last_ts_{0};
    uint64_t last_signal_ts_{0};

    void emit(int dir, double px, const char* reason, uint64_t ts) {
        TradeSignal s{
            "XAUUSD_MEAN_REVERT",
            "XAUUSD",
            dir,
            SIZE_MULT,
            px,
            reason,
            ts
        };

        std::printf("[SIGNAL][XAUUSD] %s dir=%d size=%.2f px=%.2f\n",
               s.reason, s.direction, s.size_mult, px);
        
        if (callback_) callback_(s);
    }
};

// =============================================================================
// XAUUSD ENGINE 2: Stop Fade (Stop Sweep Exhaustion)
// =============================================================================

class XAUUSDStopFade final : public EngineCRTP<XAUUSDStopFade> {
public:
    // === BACKTEST-LOCKED SETTINGS ===
    static constexpr double   MIN_SWEEP      = 0.50;   // $ minimum sweep
    static constexpr uint64_t STALL_WINDOW   = 400;    // ms
    static constexpr double   STALL_EPSILON  = 0.10;   // $ max stall range
    static constexpr double   SIZE_MULT      = 1.25;

    explicit XAUUSDStopFade(SignalCallback cb = nullptr) : callback_(cb) {}
    void setCallback(SignalCallback cb) { callback_ = cb; }

    void on_tick_impl(const Tick& t) {
        if (!is_gold_optimal_session(t.ts_ns))
            return;
            
        uint64_t now_ms = ns_to_ms(t.ts_ns);

        // Only trade in stop sweep regime
        if (t.regime != Regime::STOP_SWEEP) {
            sweep_ts_ = 0;
            sweep_px_ = 0.0;
            stall_high_ = 0.0;
            stall_low_ = 0.0;
            stall_start_ = 0;
            return;
        }

        // Track stall
        if (stall_start_ == 0) {
            stall_start_ = now_ms;
            stall_high_ = t.price;
            stall_low_ = t.price;
        } else {
            stall_high_ = std::max(stall_high_, t.price);
            stall_low_ = std::min(stall_low_, t.price);
            
            // Reset stall if outside window
            if (now_ms - stall_start_ > STALL_WINDOW) {
                stall_start_ = now_ms;
                stall_high_ = t.price;
                stall_low_ = t.price;
            }
        }

        // Initialize sweep tracking
        if (sweep_ts_ == 0) {
            sweep_ts_ = now_ms;
            sweep_px_ = t.price;
            return;
        }

        // Check for sweep + stall pattern
        double move = t.price - sweep_px_;
        bool is_stall = (stall_high_ - stall_low_) < STALL_EPSILON;
        
        if (std::fabs(move) >= MIN_SWEEP && is_stall) {
            int dir = (move > 0) ? -1 : +1;
            emit(dir, t.price, "stop_fade_exhaust", t.ts_ns);
            
            sweep_ts_ = 0;
            sweep_px_ = 0.0;
            stall_start_ = 0;
        }

        // Reset sweep if too old
        if (now_ms - sweep_ts_ > 2000) {
            sweep_ts_ = now_ms;
            sweep_px_ = t.price;
        }
    }
    
    void reset_impl() {
        sweep_ts_ = 0;
        sweep_px_ = 0.0;
        stall_high_ = 0.0;
        stall_low_ = 0.0;
        stall_start_ = 0;
    }

private:
    SignalCallback callback_;
    uint64_t sweep_ts_{0};
    double sweep_px_{0.0};
    double stall_high_{0.0};
    double stall_low_{0.0};
    uint64_t stall_start_{0};

    void emit(int dir, double px, const char* reason, uint64_t ts) {
        TradeSignal s{
            "XAUUSD_STOP_FADE",
            "XAUUSD",
            dir,
            SIZE_MULT,
            px,
            reason,
            ts
        };

        std::printf("[SIGNAL][XAUUSD] %s dir=%d size=%.2f px=%.2f\n",
               s.reason, s.direction, s.size_mult, px);
        
        if (callback_) callback_(s);
    }
};

// =============================================================================
// XAUUSD ENGINE 3: Acceptance Breakout
// =============================================================================

class XAUUSDAcceptanceBO final : public EngineCRTP<XAUUSDAcceptanceBO> {
public:
    // === BACKTEST-LOCKED SETTINGS ===
    static constexpr double   ZONE_TOLERANCE = 0.25;   // $ zone width
    static constexpr uint64_t MIN_HOLD_MS    = 1000;   // 1 second minimum
    static constexpr double   SIZE_MULT      = 2.25;

    explicit XAUUSDAcceptanceBO(SignalCallback cb = nullptr) : callback_(cb) {}
    void setCallback(SignalCallback cb) { callback_ = cb; }

    void on_tick_impl(const Tick& t) {
        if (!is_gold_optimal_session(t.ts_ns))
            return;

        // Only trade in stop sweep regime (accumulation zones)
        if (t.regime != Regime::STOP_SWEEP) {
            in_zone_ = false;
            return;
        }

        uint64_t now_ms = ns_to_ms(t.ts_ns);

        if (!in_zone_) {
            zone_px_ = t.price;
            zone_start_ = now_ms;
            in_zone_ = true;
            return;
        }

        // Check if still in zone
        if (std::fabs(t.price - zone_px_) <= ZONE_TOLERANCE) {
            // Time-in-zone acceptance reached
            if (now_ms - zone_start_ >= MIN_HOLD_MS) {
                emit(+1, t.price, "acceptance_breakout", t.ts_ns);
                in_zone_ = false;
            }
        } else {
            // Price left zone, reset
            in_zone_ = false;
        }
    }
    
    void reset_impl() {
        in_zone_ = false;
        zone_px_ = 0.0;
        zone_start_ = 0;
    }

private:
    SignalCallback callback_;
    bool in_zone_{false};
    double zone_px_{0.0};
    uint64_t zone_start_{0};

    void emit(int dir, double px, const char* reason, uint64_t ts) {
        TradeSignal s{
            "XAUUSD_ACCEPTANCE_BO",
            "XAUUSD",
            dir,
            SIZE_MULT,
            px,
            reason,
            ts
        };

        std::printf("[SIGNAL][XAUUSD] %s dir=%d size=%.2f px=%.2f\n",
               s.reason, s.direction, s.size_mult, px);
        
        if (callback_) callback_(s);
    }
};

// =============================================================================
// REGIME CLASSIFIER
// =============================================================================

class RegimeClassifier {
public:
    static constexpr double SPREAD_SWEEP_THRESH = 1.0;   // XAUUSD
    static constexpr double SPREAD_DEAD_THRESH  = 0.80;
    static constexpr double MOVE_EXP_THRESH     = 0.5;
    static constexpr double SPREAD_INV_THRESH   = 0.20;

    Regime classify(double bid, double ask, double last_price) {
        double spread = ask - bid;
        double move = std::fabs(last_price - last_);
        last_ = last_price;

        if (spread > SPREAD_SWEEP_THRESH) return Regime::STOP_SWEEP;
        if (move > MOVE_EXP_THRESH) return Regime::STRUCTURAL_EXPANSION;
        if (spread < SPREAD_INV_THRESH) return Regime::INVENTORY_CORRECTION;
        if (spread > SPREAD_DEAD_THRESH && move < 0.1) return Regime::DEAD;
        
        return Regime::RANGE_VOLATILE;
    }

private:
    double last_{0.0};
};

// =============================================================================
// NAS100 SYSTEM (Aggregator)
// =============================================================================

class NAS100System {
public:
    NAS100OpeningRangeGate gate;
    NAS100SweepReversion   sweep{gate};

    void setCallback(SignalCallback cb) {
        sweep.setCallback(cb);
    }

    void on_tick(const Tick& t) {
        gate.on_tick(t);
        sweep.on_tick(t);
    }
    
    void reset() {
        gate.reset();
        sweep.reset();
    }
};

// =============================================================================
// US30 SYSTEM (Aggregator)
// =============================================================================

class US30System {
public:
    US30OpeningRangeGate gate;
    US30SweepReversion   sweep{gate};

    void setCallback(SignalCallback cb) {
        sweep.setCallback(cb);
    }

    void on_tick(const Tick& t) {
        gate.on_tick(t);
        sweep.on_tick(t);
    }
    
    void reset() {
        gate.reset();
        sweep.reset();
    }
};

// =============================================================================
// XAUUSD SYSTEM (Aggregator)
// =============================================================================

class XAUUSDSystem {
public:
    RegimeClassifier  classifier;
    XAUUSDMeanRevert  mean_revert;
    XAUUSDStopFade    stop_fade;
    XAUUSDAcceptanceBO acceptance;

    void setCallback(SignalCallback cb) {
        mean_revert.setCallback(cb);
        stop_fade.setCallback(cb);
        acceptance.setCallback(cb);
    }

    void on_tick(double bid, double ask, uint64_t ts_ns) {
        double mid = (bid + ask) / 2.0;
        Regime r = classifier.classify(bid, ask, mid);
        
        Tick t;
        t.ts_ns = ts_ns;
        t.price = mid;
        t.bid = bid;
        t.ask = ask;
        t.volume = 1.0;
        t.regime = r;
        
        mean_revert.on_tick(t);
        stop_fade.on_tick(t);
        acceptance.on_tick(t);
    }
    
    void reset() {
        mean_revert.reset();
        stop_fade.reset();
        acceptance.reset();
    }
};

// =============================================================================
// UNIFIED CHIMERA SYSTEM
// =============================================================================

class ChimeraUnifiedSystem {
public:
    NAS100System nas100;
    US30System   us30;
    XAUUSDSystem xauusd;
    
    // Statistics
    std::atomic<uint64_t> nas100_ticks{0};
    std::atomic<uint64_t> us30_ticks{0};
    std::atomic<uint64_t> xauusd_ticks{0};
    std::atomic<uint64_t> signals_total{0};

    void setCallback(SignalCallback cb) {
        callback_ = cb;
        
        auto wrapped = [this, cb](const TradeSignal& s) {
            signals_total.fetch_add(1);
            if (cb) cb(s);
        };
        
        nas100.setCallback(wrapped);
        us30.setCallback(wrapped);
        xauusd.setCallback(wrapped);
    }

    void onNAS100Tick(double bid, double ask, uint64_t ts_ns) {
        nas100_ticks.fetch_add(1);
        
        double mid = (bid + ask) / 2.0;
        Tick t;
        t.ts_ns = ts_ns;
        t.price = mid;
        t.bid = bid;
        t.ask = ask;
        t.volume = 1.0;
        t.regime = Regime::RANGE_VOLATILE;  // Index doesn't use regime
        
        nas100.on_tick(t);
    }

    void onUS30Tick(double bid, double ask, uint64_t ts_ns) {
        us30_ticks.fetch_add(1);
        
        double mid = (bid + ask) / 2.0;
        Tick t;
        t.ts_ns = ts_ns;
        t.price = mid;
        t.bid = bid;
        t.ask = ask;
        t.volume = 1.0;
        t.regime = Regime::RANGE_VOLATILE;
        
        us30.on_tick(t);
    }

    void onXAUUSDTick(double bid, double ask, uint64_t ts_ns) {
        xauusd_ticks.fetch_add(1);
        xauusd.on_tick(bid, ask, ts_ns);
    }
    
    void resetDaily() {
        nas100.reset();
        us30.reset();
        xauusd.reset();
        std::printf("[CHIMERA] Daily reset complete\n");
    }
    
    void printStats() {
        std::printf("[CHIMERA] Stats: NAS100=%lu US30=%lu XAUUSD=%lu signals=%lu\n",
                    nas100_ticks.load(), us30_ticks.load(), 
                    xauusd_ticks.load(), signals_total.load());
    }

private:
    SignalCallback callback_;
};

} // namespace chimera
