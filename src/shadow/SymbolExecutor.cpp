#include "shadow/SymbolExecutor.hpp"
#include "execution/ExecutionRouter.hpp"
#include "risk/SessionGuard.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <deque>

namespace shadow {

// === INLINE ATR CALCULATOR ===
class SimpleATR {
public:
    static constexpr int PERIOD = 20;
    std::deque<double> highs_;
    std::deque<double> lows_;
    double last_close_ = 0.0;
    
    void update(double high, double low, double close) {
        highs_.push_back(high);
        lows_.push_back(low);
        if (highs_.size() > PERIOD) { highs_.pop_front(); lows_.pop_front(); }
        last_close_ = close;
    }
    
    double get() const {
        if (highs_.size() < 2) return 6.0; // Default for XAU
        double sum = 0.0;
        for (size_t i = 0; i < highs_.size(); ++i) {
            sum += highs_[i] - lows_[i];
        }
        return sum / highs_.size();
    }
    
    double get_ref() const {
        return get() * 0.8; // Reference ATR = 80% of current
    }
};

// === TRADE PERMISSION GATE (INLINE) ===
enum class TradeBlockReason {
    NONE, SESSION_NOT_ARMED, VOLATILITY_SHOCK, SYMBOL_MUTED,
    REJECT_FUSE, IMPULSE_NOT_PERSISTENT, ASIA_DISABLED
};

const char* reasonToString(TradeBlockReason r) {
    switch(r) {
        case TradeBlockReason::NONE: return "NONE";
        case TradeBlockReason::SESSION_NOT_ARMED: return "SESSION_NOT_ARMED";
        case TradeBlockReason::VOLATILITY_SHOCK: return "VOLATILITY_SHOCK";
        case TradeBlockReason::SYMBOL_MUTED: return "SYMBOL_MUTED";
        case TradeBlockReason::REJECT_FUSE: return "REJECT_FUSE";
        case TradeBlockReason::IMPULSE_NOT_PERSISTENT: return "IMPULSE_NOT_PERSISTENT";
        case TradeBlockReason::ASIA_DISABLED: return "ASIA_DISABLED";
    }
    return "UNKNOWN";
}

struct TradeContext {
    const char* symbol;
    double impulse;
    double velocity;
    uint64_t now_ns;
};

class TradePermissionGate {
public:
    static TradePermissionGate& instance() {
        static TradePermissionGate gate;
        return gate;
    }
    
    struct SymbolState {
        bool session_armed = false;
        bool volatility_shock = false;
        bool asia_disabled = false;
        uint64_t mute_until_ns = 0;
        uint32_t rejects = 0;
        uint64_t reject_window_start_ns = 0;
        double last_impulse = 0.0;
        uint64_t impulse_start_ns = 0;
        uint32_t gate_blocks = 0;
    };
    
    std::unordered_map<std::string, SymbolState> states_;
    
    static constexpr uint64_t IMPULSE_PERSIST_NS = 400'000'000;
    static constexpr double IMPULSE_MIN = 0.08;
    static constexpr uint32_t REJECT_LIMIT = 10;
    static constexpr uint64_t MUTE_NS = 60'000'000'000;
    
    SymbolState& state(const char* symbol) { return states_[std::string(symbol)]; }
    
    bool impulsePersistent(SymbolState& s, double impulse, uint64_t now_ns) {
        if (impulse < IMPULSE_MIN) { s.impulse_start_ns = 0; s.last_impulse = impulse; return false; }
        if (s.impulse_start_ns == 0 || impulse < s.last_impulse) { s.impulse_start_ns = now_ns; s.last_impulse = impulse; return false; }
        s.last_impulse = impulse;
        return (now_ns - s.impulse_start_ns) >= IMPULSE_PERSIST_NS;
    }
    
    bool allow(const TradeContext& ctx, TradeBlockReason& reason) {
        auto& s = state(ctx.symbol);
        if (!s.session_armed) { reason = TradeBlockReason::SESSION_NOT_ARMED; s.gate_blocks++; if (s.gate_blocks % 100 == 1) std::cout << "[GATE] " << ctx.symbol << " BLOCKED: " << reasonToString(reason) << " (count=" << s.gate_blocks << ")\n"; return false; }
        if (s.asia_disabled) { reason = TradeBlockReason::ASIA_DISABLED; s.gate_blocks++; if (s.gate_blocks % 50 == 1) std::cout << "[GATE] " << ctx.symbol << " BLOCKED: " << reasonToString(reason) << " (count=" << s.gate_blocks << ")\n"; return false; }
        if (s.volatility_shock) { reason = TradeBlockReason::VOLATILITY_SHOCK; s.gate_blocks++; if (s.gate_blocks % 50 == 1) std::cout << "[GATE] " << ctx.symbol << " BLOCKED: " << reasonToString(reason) << " (count=" << s.gate_blocks << ")\n"; return false; }
        if (ctx.now_ns < s.mute_until_ns) { reason = TradeBlockReason::SYMBOL_MUTED; return false; }
        if (!impulsePersistent(s, ctx.impulse, ctx.now_ns)) { reason = TradeBlockReason::IMPULSE_NOT_PERSISTENT; return false; }
        reason = TradeBlockReason::NONE; s.gate_blocks = 0; return true;
    }
    
    void onReject(const char* symbol) {
        auto& s = state(symbol);
        if (s.rejects == 0) s.reject_window_start_ns = s.impulse_start_ns;
        s.rejects++;
        std::cout << "[GATE] " << symbol << " REJECT (count=" << s.rejects << ")\n";
        if (s.rejects >= REJECT_LIMIT) { s.mute_until_ns = s.reject_window_start_ns + MUTE_NS; s.rejects = 0; s.reject_window_start_ns = 0; std::cout << "[MUTE] " << symbol << " (reject fuse)\n"; }
    }
    
    void onFill(const char* symbol) {
        auto& s = state(symbol);
        s.rejects = 0; s.reject_window_start_ns = 0;
        std::cout << "[GATE] " << symbol << " FILL\n";
    }
    
    void onSessionArm(const char* symbol) {
        state(symbol).session_armed = true;
        std::cout << "[GATE] " << symbol << " SESSION_ARMED\n";
    }
    
    void onVolatilityShock(const char* symbol, bool active) {
        auto& s = state(symbol);
        bool changed = (s.volatility_shock != active);
        s.volatility_shock = active;
        if (changed && active) std::cout << "[GATE] " << symbol << " VOLATILITY_SHOCK ACTIVE\n";
        else if (changed && !active) std::cout << "[GATE] " << symbol << " VOLATILITY_SHOCK CLEARED\n";
    }
    
    void onAsiaDisable(const char* symbol, bool disabled) {
        auto& s = state(symbol);
        bool changed = (s.asia_disabled != disabled);
        s.asia_disabled = disabled;
        if (changed && disabled) std::cout << "[GATE] " << symbol << " ASIA_DISABLED\n";
        else if (changed && !disabled) std::cout << "[GATE] " << symbol << " ASIA_ENABLED\n";
    }
};

// === SESSION DETECTION ===
struct SessionClock {
    static bool is_asia(uint64_t now_ns) { uint64_t h = ((now_ns / 1'000'000'000ULL) / 3600) % 24; return (h >= 22 || h <= 6); }
    static bool is_tokyo(uint64_t now_ns) { uint64_t h = ((now_ns / 1'000'000'000ULL) / 3600) % 24; return (h >= 23 || h <= 1); }
    static bool is_london(uint64_t now_ns) { uint64_t h = ((now_ns / 1'000'000'000ULL) / 3600) % 24; return (h >= 7 && h <= 16); }
    static int minutes_to_london_open(uint64_t now_ns) { uint64_t s = now_ns / 1'000'000'000ULL; uint64_t h = (s / 3600) % 24; if (h >= 7) return 0; return (7 - h) * 60 - ((s % 3600) / 60); }
    static const char* get_session_name(uint64_t now_ns) { if (is_london(now_ns)) return "LONDON"; if (is_asia(now_ns)) return "ASIA"; return "OFF_HOURS"; }
};

// === ENTRY GOVERNOR ===
struct MarketState { double impulse; double velocity; double atr; uint64_t now_ns; bool shock; bool asia_session; bool session_loaded; int current_legs; };
struct EntryDecision { bool allow; const char* reason; };

class XAUEntryGovernor {
public:
    uint64_t impulse_ok_since_ = 0;
    uint64_t cooldown_until_ = 0;
    static constexpr double IMPULSE_ASIA = 0.12, IMPULSE_LONDON = 0.08;
    static constexpr uint64_t SHOCK_COOLDOWN_NS = 1'500'000'000;
    static constexpr double ATR_PER_LEG = 2.5;
    static constexpr int MAX_LEGS_HARD = 3;
    
    EntryDecision evaluate(const MarketState& m) {
        if (!m.session_loaded) return {false, "SESSION_NOT_READY"};
        if (m.shock) { cooldown_until_ = m.now_ns + SHOCK_COOLDOWN_NS; impulse_ok_since_ = 0; return {false, "VOLATILITY_SHOCK"}; }
        if (m.now_ns < cooldown_until_) return {false, "SHOCK_COOLDOWN"};
        double min_impulse = m.asia_session ? IMPULSE_ASIA : IMPULSE_LONDON;
        if (m.impulse < min_impulse) { impulse_ok_since_ = 0; return {false, "IMPULSE_TOO_WEAK"}; }
        int max_legs = compute_max_legs(m.atr, m.asia_session);
        if (m.current_legs >= max_legs) return {false, "ATR_LEG_LIMIT"};
        return {true, "ENTRY_OK"};
    }
    
    int compute_max_legs(double atr, bool asia) {
        int legs = int(atr / ATR_PER_LEG);
        if (legs < 1) legs = 1;
        if (legs > MAX_LEGS_HARD) legs = MAX_LEGS_HARD;
        if (asia) legs = 1;
        return legs;
    }
};

// === SHOCK STATE ===
enum class ShockState { NORMAL, SHOCK, COOLDOWN };
struct VolatilityShock {
    ShockState state = ShockState::NORMAL;
    uint64_t shock_ns = 0;
    static constexpr uint64_t SHOCK_HOLD_NS = 5'000'000'000ULL, COOLDOWN_NS = 8'000'000'000ULL;
    
    void update(double atr, double atr_ref, double impulse, double velocity, double latency_ms, uint64_t now_ns) {
        int flags = 0;
        if (atr_ref > 0.0 && atr / atr_ref > 2.5) flags++;
        if (impulse > 0.20) flags++;
        if (latency_ms > 8.0) flags++;
        if (std::fabs(velocity) < 0.01 && impulse > 0.10) flags++;
        if (flags >= 2 && state == ShockState::NORMAL) { state = ShockState::SHOCK; shock_ns = now_ns; std::cout << "[SHOCK] VOLATILITY DETECTED\n"; }
    }
    
    void decay(uint64_t now_ns) {
        if (state == ShockState::SHOCK && now_ns - shock_ns > SHOCK_HOLD_NS) { state = ShockState::COOLDOWN; shock_ns = now_ns; std::cout << "[SHOCK] → COOLDOWN\n"; }
        if (state == ShockState::COOLDOWN && now_ns - shock_ns > COOLDOWN_NS) { state = ShockState::NORMAL; std::cout << "[SHOCK] → NORMAL\n"; }
    }
    
    bool is_shock() const { return state == ShockState::SHOCK; }
};

// === SESSION ARMING ===
struct SessionArmer {
    uint64_t first_quote_ns = 0, armed_ns = 0;
    bool armed = false, notified = false;
    static constexpr uint64_t WARMUP_NS = 180'000'000;
    void on_quote(uint64_t now_ns) { if (first_quote_ns == 0) first_quote_ns = now_ns; if (!armed && now_ns - first_quote_ns >= WARMUP_NS) { armed = true; armed_ns = now_ns; } }
    void reset() { first_quote_ns = 0; armed_ns = 0; armed = false; notified = false; }
    bool allow() const { return armed; }
};

// === TOKYO RAMP ===
struct TokyoRamp {
    uint64_t open_ns = 0; bool active = false;
    static constexpr uint64_t RAMP_NS = 900'000'000;
    void on_session(bool is_tokyo, uint64_t now_ns) { if (is_tokyo && !active) { open_ns = now_ns; active = true; } if (!is_tokyo) { open_ns = 0; active = false; } }
    double size_scale(uint64_t now_ns) const { if (!active) return 1.0; double t = double(now_ns - open_ns) / double(RAMP_NS); if (t <= 0.0) return 0.3; if (t >= 1.0) return 1.0; return 0.3 + 0.7 * t; }
    bool allow(uint64_t now_ns) const { if (!active) return true; return (now_ns - open_ns) > 120'000'000; }
};

// === ASIA TP DECAY ===
struct AsiaTPDecay {
    static constexpr uint64_t START_NS = 300'000'000, FULL_NS = 900'000'000;
    double scale(uint64_t age_ns, bool asia) const { if (!asia) return 1.0; if (age_ns <= START_NS) return 1.0; if (age_ns >= FULL_NS) return 0.4; double t = double(age_ns - START_NS) / double(FULL_NS - START_NS); return 1.0 - 0.6 * t; }
};

// === ASIA FAILSAFE ===
struct AsiaFailSafe {
    int losses = 0; bool disabled = false;
    void on_exit(double pnl, bool asia, bool london) { if (asia && pnl < 0.0) { losses++; if (losses >= 2) { disabled = true; std::cout << "[ASIA] AUTO-DISABLED\n"; } } if (london && disabled) { losses = 0; disabled = false; std::cout << "[ASIA] RE-ARMED\n"; } }
    void on_session_change(bool asia) { if (!asia) { losses = 0; disabled = false; } }
    bool allow() const { return !disabled; }
};

// === LONDON BOOST ===
struct LondonBoost {
    static constexpr uint64_t WINDOW_NS = 1'800'000'000;
    double scale(uint64_t since_open_ns, bool london, bool fast) const { if (!london || !fast) return 1.0; if (since_open_ns > WINDOW_NS) return 1.0; return 1.25; }
};

// === EXECUTION SURVIVAL ===
enum class ExecRegime { FAST, SLOW, HALT };
struct HaltControl { bool active = false; uint64_t entered_ns = 0; bool trimmed = false; };

class ExecutionSurvival {
public:
    ExecRegime regime = ExecRegime::FAST; HaltControl halt;
    void update_regime(uint64_t now_ns, double p95_ms) { ExecRegime prev = regime; if (p95_ms >= 200.0) regime = ExecRegime::HALT; else if (p95_ms >= 20.0) regime = ExecRegime::SLOW; else regime = ExecRegime::FAST; if (regime != prev && regime == ExecRegime::HALT) { halt.active = true; halt.entered_ns = now_ns; halt.trimmed = false; } else if (regime == ExecRegime::FAST) { halt.active = false; } }
    bool allow_entry() const { return regime == ExecRegime::FAST; }
    bool should_trim_halt(uint64_t now_ns, double pnl, int legs, double& trim_frac) { if (!halt.active || halt.trimmed || legs == 0) return false; if (now_ns - halt.entered_ns < 500'000'000) return false; if (pnl > -1.50) return false; trim_frac = 0.50; halt.trimmed = true; return true; }
    bool should_exit_chop(double pnl, double vel, int legs) const { return (regime == ExecRegime::FAST && legs >= 2 && pnl <= -2.00 && std::fabs(vel) < 0.05); }
};

// === POSITION FAILURE ===
struct FailureState { bool armed = false; uint64_t armed_at_ns = 0; };
class PositionFailure {
public:
    FailureState state;
    void maybe_arm(uint64_t now_ns, int legs, int max_legs, double impulse, double velocity, double pnl, ExecRegime regime) { if (regime != ExecRegime::FAST) return; if (legs < max_legs) return; if (impulse < 0.08 && std::fabs(velocity) < 0.07 && pnl < 0.50) { if (!state.armed) { state.armed = true; state.armed_at_ns = now_ns; } } else { state.armed = false; } }
    bool should_trim(uint64_t now_ns, double pnl, double& trim_frac) { if (!state.armed) return false; if (now_ns - state.armed_at_ns < 750'000'000) return false; if (pnl <= -4.50) { trim_frac = 1.0; state.armed = false; return true; } trim_frac = 0.66; state.armed = false; return true; }
};

static SessionArmer g_session_armer;
static TokyoRamp g_tokyo_ramp;
static AsiaTPDecay g_asia_tp_decay;
static AsiaFailSafe g_asia_failsafe;
static LondonBoost g_london_boost;
static VolatilityShock g_vol_shock;
static ExecutionSurvival g_survival;
static PositionFailure g_failure;
static XAUEntryGovernor g_entry_governor;
static SimpleATR g_atr;

SymbolExecutor::SymbolExecutor(const SymbolConfig& cfg, ExecMode mode, ExecutionRouter& router)
    : cfg_(cfg), mode_(mode), ledger_(), governor_(ledger_), session_guard_({86400, 0, 0})
    , metal_type_(cfg.symbol == "XAUUSD" ? Metal::XAU : Metal::XAG)
    , router_(router), profit_governor_(), realized_pnl_(0.0)
    , last_entry_ts_(0), trades_this_hour_(0), hour_start_ts_(0)
    , last_bid_(0.0), last_ask_(0.0), last_latency_ms_(10.0)
    , account_equity_(100000.0)
{}

void SymbolExecutor::onSignal(const Signal& s, uint64_t ts_ms) {
    uint64_t now_ns = ts_ms * 1'000'000;
    double velocity = router_.get_velocity(cfg_.symbol);
    double impulse = std::fabs(velocity);
    
    TradeContext ctx; ctx.symbol = cfg_.symbol.c_str(); ctx.impulse = impulse; ctx.velocity = velocity; ctx.now_ns = now_ns;
    TradeBlockReason reason;
    if (!TradePermissionGate::instance().allow(ctx, reason)) return;
    
    if (!canEnter(s, ts_ms)) { TradePermissionGate::instance().onReject(cfg_.symbol.c_str()); return; }
    
    double entry_price = (s.side == Side::BUY) ? last_ask_ : last_bid_;
    enterBase(s.side, entry_price, ts_ms);
    TradePermissionGate::instance().onFill(cfg_.symbol.c_str());
}

bool SymbolExecutor::canEnter(const Signal& s, uint64_t ts_ms) {
    uint64_t now_ns = ts_ms * 1'000'000;
    g_session_armer.on_quote(now_ns);
    if (!g_survival.allow_entry()) return false;
    
    int current_legs = static_cast<int>(legs_.size());
    double velocity = router_.get_velocity(cfg_.symbol);
    double impulse = std::fabs(velocity);
    bool asia = SessionClock::is_asia(now_ns), tokyo = SessionClock::is_tokyo(now_ns), london = SessionClock::is_london(now_ns);
    
    if (metal_type_ == Metal::XAU) {
        if (asia && !g_asia_failsafe.allow()) { rejection_stats_.total_rejections++; return false; }
        
        double atr = g_atr.get(), atr_ref = g_atr.get_ref();
        g_vol_shock.update(atr, atr_ref, impulse, velocity, last_latency_ms_, now_ns);
        g_vol_shock.decay(now_ns);
        
        g_tokyo_ramp.on_session(tokyo, now_ns);
        if (!g_tokyo_ramp.allow(now_ns)) { rejection_stats_.total_rejections++; return false; }
        
        MarketState mkt; mkt.impulse = impulse; mkt.velocity = velocity; mkt.atr = atr; mkt.now_ns = now_ns; mkt.shock = g_vol_shock.is_shock(); mkt.asia_session = asia; mkt.session_loaded = g_session_armer.allow(); mkt.current_legs = current_legs;
        EntryDecision decision = g_entry_governor.evaluate(mkt);
        if (!decision.allow) { rejection_stats_.total_rejections++; return false; }
        return true;
    }
    
    if (metal_type_ == Metal::XAG) {
        if (!london || current_legs >= 1 || impulse < 0.14 || velocity < 0.09) { rejection_stats_.total_rejections++; return false; }
        return true;
    }
    return false;
}

void SymbolExecutor::enterBase(Side side, double price, uint64_t ts) {
    uint64_t now_ns = ts * 1'000'000;
    bool london = SessionClock::is_london(now_ns);
    double base_size = (metal_type_ == Metal::XAU) ? 1.0 : 0.5;
    base_size *= g_tokyo_ramp.size_scale(now_ns);
    base_size *= g_london_boost.scale(0, london, last_latency_ms_ <= 7.0);
    double stop_distance = (metal_type_ == Metal::XAU) ? 2.20 : 0.15, tp_distance = (metal_type_ == Metal::XAU) ? 3.50 : 0.25;
    double stop = (side == Side::BUY) ? price - stop_distance : price + stop_distance;
    double take_profit = (side == Side::BUY) ? price + tp_distance : price - tp_distance;
    profit_governor_.init_stop(price, side == Side::BUY);
    
    Leg leg; leg.side = side; leg.entry = price; leg.size = base_size; leg.stop = stop; leg.take_profit = take_profit; leg.entry_impulse = std::fabs(router_.get_velocity(cfg_.symbol)); leg.entry_ts = ts;
    legs_.push_back(leg);
    size_t leg_index = legs_.size() - 1;
    uint64_t trade_id = ts + leg_index;
    leg_to_trade_[leg_index] = trade_id;
    last_entry_ts_ = ts; trades_this_hour_++;
    std::cout << "[" << cfg_.symbol << "] ENTRY trade_id=" << trade_id << " side=" << (side == Side::BUY ? "BUY" : "SELL") << " price=" << price << " size=" << base_size << " legs=" << legs_.size() << "\n";
}

void SymbolExecutor::onTick(const Tick& t) {
    last_bid_ = t.bid; last_ask_ = t.ask;
    g_atr.update(t.ask, t.bid, (t.bid + t.ask) / 2.0);
    
    Quote q; q.bid = t.bid; q.ask = t.ask; q.ts_ms = t.ts_ms;
    router_.on_quote(cfg_.symbol, q);
    
    uint64_t current_hour = t.ts_ms / 3600000;
    if (current_hour != hour_start_ts_ / 3600000) { trades_this_hour_ = 0; hour_start_ts_ = t.ts_ms; }
    
    uint64_t now_ns = t.ts_ms * 1'000'000;
    g_survival.update_regime(now_ns, last_latency_ms_);
    
    bool asia = SessionClock::is_asia(now_ns), london = SessionClock::is_london(now_ns);
    int minutes_to_london = SessionClock::minutes_to_london_open(now_ns);
    
    if (g_session_armer.allow() && !g_session_armer.notified) { TradePermissionGate::instance().onSessionArm(cfg_.symbol.c_str()); g_session_armer.notified = true; }
    TradePermissionGate::instance().onVolatilityShock(cfg_.symbol.c_str(), g_vol_shock.is_shock());
    TradePermissionGate::instance().onAsiaDisable(cfg_.symbol.c_str(), !g_asia_failsafe.allow() && asia);
    
    double unrealized_pnl = 0.0;
    for (const auto& leg : legs_) { bool is_long = (leg.side == Side::BUY); double current_price = is_long ? t.bid : t.ask; double leg_pnl = is_long ? (current_price - leg.entry) * leg.size : (leg.entry - current_price) * leg.size; unrealized_pnl += leg_pnl; }
    
    double velocity = router_.get_velocity(cfg_.symbol), impulse = std::fabs(velocity);
    int current_legs = legs_.size();
    
    if (asia && minutes_to_london <= 10 && current_legs > 0) { exitAll("ASIA_END_FLATTEN", (t.bid + t.ask) / 2.0, t.ts_ms); return; }
    
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i]; uint64_t age_ns = now_ns - (leg.entry_ts * 1'000'000);
        if (age_ns > 180'000'000) {
            double impulse_thresh = asia ? 0.04 : 0.08, vel_thresh = asia ? 0.02 : 0.05;
            if (impulse < impulse_thresh && std::fabs(velocity) < vel_thresh) {
                double exit_price = (leg.side == Side::BUY) ? t.bid : t.ask;
                double pnl = (leg.side == Side::BUY) ? (exit_price - leg.entry) * leg.size : (leg.entry - exit_price) * leg.size;
                realized_pnl_ += pnl;
                std::cout << "[" << cfg_.symbol << "] EXIT MOMENTUM_DECAY pnl=$" << pnl << "\n";
                g_asia_failsafe.on_exit(pnl, asia, london);
                legs_.erase(legs_.begin() + i); leg_to_trade_.erase(i); continue;
            }
        }
        if (asia) { uint64_t age_ns = now_ns - (leg.entry_ts * 1'000'000); double tp_scale = g_asia_tp_decay.scale(age_ns, asia); bool is_long = (leg.side == Side::BUY); double original_tp = leg.take_profit; double new_tp = is_long ? leg.entry + (original_tp - leg.entry) * tp_scale : leg.entry - (leg.entry - original_tp) * tp_scale; leg.take_profit = new_tp; }
        i++;
    }
    
    if (metal_type_ == Metal::XAU) {
        int max_legs = g_entry_governor.compute_max_legs(g_atr.get(), asia);
        g_failure.maybe_arm(now_ns, current_legs, max_legs, impulse, velocity, unrealized_pnl, g_survival.regime);
        double trim_frac = 0.0;
        if (g_failure.should_trim(now_ns, unrealized_pnl, trim_frac)) {
            int to_trim = static_cast<int>(legs_.size() * trim_frac); if (trim_frac >= 1.0) to_trim = legs_.size();
            for (int i = 0; i < to_trim && !legs_.empty(); i++) { double exit_price = (legs_[0].side == Side::BUY) ? t.bid : t.ask; double pnl = (legs_[0].side == Side::BUY) ? (exit_price - legs_[0].entry) * legs_[0].size : (legs_[0].entry - exit_price) * legs_[0].size; realized_pnl_ += pnl; std::cout << "[" << cfg_.symbol << "] FAILURE_EXIT pnl=$" << pnl << "\n"; g_asia_failsafe.on_exit(pnl, asia, london); legs_.erase(legs_.begin()); leg_to_trade_.erase(0); }
            if (legs_.empty()) return;
        }
    }
    
    if (g_survival.should_exit_chop(unrealized_pnl, velocity, current_legs)) { exitAll("CHOP_SHIELD", (t.bid + t.ask) / 2.0, t.ts_ms); return; }
    
    double halt_trim_frac = 0.0;
    if (g_survival.should_trim_halt(now_ns, unrealized_pnl, current_legs, halt_trim_frac)) {
        int to_trim = static_cast<int>(legs_.size() * halt_trim_frac);
        for (int i = 0; i < to_trim && !legs_.empty(); i++) { double exit_price = (legs_[0].side == Side::BUY) ? t.bid : t.ask; double pnl = (legs_[0].side == Side::BUY) ? (exit_price - legs_[0].entry) * legs_[0].size : (legs_[0].entry - exit_price) * legs_[0].size; realized_pnl_ += pnl; std::cout << "[" << cfg_.symbol << "] HALT_TRIM pnl=$" << pnl << "\n"; legs_.erase(legs_.begin()); leg_to_trade_.erase(0); }
    }
    
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i]; bool is_long = (leg.side == Side::BUY);
        bool hit_stop = (is_long && t.bid <= leg.stop) || (!is_long && t.ask >= leg.stop);
        bool hit_tp = (is_long && t.bid >= leg.take_profit) || (!is_long && t.ask <= leg.take_profit);
        if (hit_stop || hit_tp) {
            double exit_price = is_long ? t.bid : t.ask;
            double pnl = is_long ? (exit_price - leg.entry) * leg.size : (leg.entry - exit_price) * leg.size;
            uint64_t trade_id = leg_to_trade_[i]; realized_pnl_ += pnl;
            const char* reason = hit_stop ? "SL" : "TP";
            std::cout << "[" << cfg_.symbol << "] EXIT " << reason << " trade_id=" << trade_id << " pnl=$" << pnl << "\n";
            g_asia_failsafe.on_exit(pnl, asia, london); profit_governor_.on_exit(now_ns);
            if (exit_callback_) exit_callback_(cfg_.symbol.c_str(), trade_id, exit_price, pnl, reason);
            if (gui_callback_) { char side_char = (leg.side == Side::BUY) ? 'B' : 'S'; gui_callback_(cfg_.symbol.c_str(), trade_id, side_char, leg.entry, exit_price, leg.size, pnl, t.ts_ms); }
            legs_.erase(legs_.begin() + i); leg_to_trade_.erase(i); continue;
        }
        i++;
    }
}

void SymbolExecutor::exitAll(const char* reason, double price, uint64_t ts) {
    uint64_t now_ns = ts * 1'000'000; bool asia = SessionClock::is_asia(now_ns), london = SessionClock::is_london(now_ns);
    for (size_t i = 0; i < legs_.size(); i++) { auto& leg = legs_[i]; uint64_t trade_id = leg_to_trade_[i]; double pnl = (leg.side == Side::BUY) ? (price - leg.entry) * leg.size : (leg.entry - price) * leg.size; realized_pnl_ += pnl; std::cout << "[" << cfg_.symbol << "] EXIT " << reason << " trade_id=" << trade_id << " pnl=$" << pnl << "\n"; g_asia_failsafe.on_exit(pnl, asia, london); if (exit_callback_) exit_callback_(cfg_.symbol.c_str(), trade_id, price, pnl, reason); if (gui_callback_) { char side = (leg.side == Side::BUY) ? 'B' : 'S'; gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, price, leg.size, pnl, ts); } }
    profit_governor_.on_exit(now_ns); legs_.clear(); leg_to_trade_.clear();
}

double SymbolExecutor::getRealizedPnL() const { return realized_pnl_; }
void SymbolExecutor::setGUICallback(GUITradeCallback cb) { gui_callback_ = cb; }
void SymbolExecutor::setExitCallback(ExitCallback cb) { exit_callback_ = cb; }
int SymbolExecutor::getActiveLegs() const { return static_cast<int>(legs_.size()); }

void SymbolExecutor::status() const {
    std::cout << "[" << cfg_.symbol << "] legs=" << legs_.size() << " pnl=$" << realized_pnl_ << " trades=" << trades_this_hour_ << " rejects=" << rejection_stats_.total_rejections << "\n";
}

} // namespace shadow
