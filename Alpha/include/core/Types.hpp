// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Core Types
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.0.0
// PURPOSE: Focused CFD profit extraction on XAUUSD + NAS100 ONLY
// ARCHITECTURE: Dual-engine (one per instrument, complete isolation)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cmath>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// VERSION INFO
// ═══════════════════════════════════════════════════════════════════════════════
constexpr const char* VERSION = "1.0.0";
constexpr const char* CODENAME = "APEX";

// ═══════════════════════════════════════════════════════════════════════════════
// THE ONLY TWO INSTRUMENTS (HARDCODED)
// ═══════════════════════════════════════════════════════════════════════════════
enum class Instrument : uint8_t {
    XAUUSD = 0,    // 🥇 Gold - THE money maker
    NAS100 = 1,    // 🥈 Nasdaq - momentum beast
    COUNT  = 2,
    INVALID = 255
};

inline const char* instrument_str(Instrument i) noexcept {
    switch (i) {
        case Instrument::XAUUSD: return "XAUUSD";
        case Instrument::NAS100: return "NAS100";
        default: return "INVALID";
    }
}

inline Instrument parse_instrument(const char* sym) noexcept {
    if (!sym) return Instrument::INVALID;
    if (std::strstr(sym, "XAU") || std::strstr(sym, "GOLD")) return Instrument::XAUUSD;
    if (std::strstr(sym, "NAS") || std::strstr(sym, "NDX") || std::strstr(sym, "US100")) return Instrument::NAS100;
    return Instrument::INVALID;
}

inline Instrument parse_instrument(const std::string& sym) noexcept {
    return parse_instrument(sym.c_str());
}

inline bool is_valid_instrument(const std::string& sym) noexcept {
    return parse_instrument(sym) != Instrument::INVALID;
}

// ═══════════════════════════════════════════════════════════════════════════════
// INSTRUMENT SPECIFICATIONS (BlackBull/cTrader)
// ═══════════════════════════════════════════════════════════════════════════════
struct InstrumentSpec {
    Instrument instrument;
    const char* symbol;
    const char* ctrader_symbol;    // cTrader symbol name (may differ)
    int security_id;                // Populated from SecurityList
    double pip_value;
    double min_lot;
    double lot_step;
    double typical_spread;         // Typical spread in points
    double wide_spread;            // "Too wide" threshold
    int digits;
    double point_value;
    double max_lots;
    
    // Position sizing helpers
    double base_risk_pct = 0.005;  // 0.5% base risk
    double peak_risk_pct = 0.012;  // 1.2% peak session risk
};

inline InstrumentSpec get_spec(Instrument i) noexcept {
    switch (i) {
        case Instrument::XAUUSD:
            return {
                Instrument::XAUUSD,
                "XAUUSD", "XAUUSD", 0,
                0.01, 0.01, 0.01,
                0.28, 0.45, 2, 1.0, 5.0,
                0.005, 0.012
            };
        case Instrument::NAS100:
            return {
                Instrument::NAS100,
                "NAS100", "NAS100", 0,
                0.1, 0.01, 0.01,
                1.0, 1.8, 1, 1.0, 2.0,
                0.004, 0.010
            };
        default:
            return {};
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SIDE
// ═══════════════════════════════════════════════════════════════════════════════
enum class Side : int8_t {
    FLAT  = 0,
    LONG  = 1,
    SHORT = -1
};

inline const char* side_str(Side s) noexcept {
    switch (s) {
        case Side::LONG:  return "LONG";
        case Side::SHORT: return "SHORT";
        default: return "FLAT";
    }
}

inline Side opposite(Side s) noexcept {
    if (s == Side::LONG) return Side::SHORT;
    if (s == Side::SHORT) return Side::LONG;
    return Side::FLAT;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TICK DATA
// ═══════════════════════════════════════════════════════════════════════════════
struct Tick {
    Instrument instrument = Instrument::INVALID;
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double spread = 0.0;
    double spread_bps = 0.0;
    uint64_t timestamp_ns = 0;
    uint64_t sequence = 0;
    
    [[nodiscard]] bool valid() const noexcept {
        return instrument != Instrument::INVALID && 
               bid > 0 && ask > 0 && bid < ask;
    }
    
    static Tick make(Instrument inst, double b, double a, uint64_t ts, uint64_t seq = 0) noexcept {
        Tick t;
        t.instrument = inst;
        t.bid = b;
        t.ask = a;
        t.mid = (b + a) / 2.0;
        t.spread = a - b;
        t.spread_bps = (t.mid > 0) ? (t.spread / t.mid * 10000.0) : 0.0;
        t.timestamp_ns = ts;
        t.sequence = seq;
        return t;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// POSITION STATE
// ═══════════════════════════════════════════════════════════════════════════════
struct Position {
    bool active = false;
    Instrument instrument = Instrument::INVALID;
    Side side = Side::FLAT;
    double entry_price = 0.0;
    double size = 0.0;
    uint64_t entry_ts = 0;
    
    // Tracking
    double highest_price = 0.0;
    double lowest_price = 0.0;
    double unrealized_pnl = 0.0;
    double unrealized_bps = 0.0;
    
    // Stop management
    double stop_price = 0.0;
    double target_price = 0.0;
    bool stop_at_breakeven = false;
    bool trailing_active = false;
    
    void update(double current_price) noexcept {
        if (!active || entry_price <= 0) return;
        
        if (side == Side::LONG) {
            unrealized_bps = (current_price - entry_price) / entry_price * 10000.0;
            highest_price = std::max(highest_price, current_price);
        } else if (side == Side::SHORT) {
            unrealized_bps = (entry_price - current_price) / entry_price * 10000.0;
            if (lowest_price == 0.0) lowest_price = current_price;
            lowest_price = std::min(lowest_price, current_price);
        }
    }
    
    [[nodiscard]] uint64_t hold_time_ms(uint64_t now_ns) const noexcept {
        if (!active || entry_ts == 0) return 0;
        return (now_ns - entry_ts) / 1'000'000;
    }
    
    void reset() noexcept {
        *this = Position{};
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ORDER INTENT
// ═══════════════════════════════════════════════════════════════════════════════
struct OrderIntent {
    Instrument instrument = Instrument::INVALID;
    Side side = Side::FLAT;
    double size = 0.0;
    double entry_price = 0.0;
    double stop_loss = 0.0;
    double take_profit = 0.0;
    uint64_t created_ts = 0;
    bool is_close = false;  // True if closing a position
    
    [[nodiscard]] bool valid() const noexcept {
        return instrument != Instrument::INVALID && 
               side != Side::FLAT && 
               size > 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// TIMING UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════
inline uint64_t now_ns() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

inline uint64_t now_ms() noexcept {
    return now_ns() / 1'000'000;
}

inline uint64_t utc_timestamp() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL KILL SWITCH
// ═══════════════════════════════════════════════════════════════════════════════
class KillSwitch {
public:
    KillSwitch() noexcept : killed_(false), kill_reason_("") {}
    
    void kill(const char* reason = "MANUAL") noexcept {
        killed_.store(true, std::memory_order_release);
        kill_reason_ = reason;
    }
    
    [[nodiscard]] bool killed() const noexcept {
        return killed_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] bool alive() const noexcept { return !killed(); }
    [[nodiscard]] const char* reason() const noexcept { return kill_reason_; }
    
    void reset() noexcept {
        killed_.store(false, std::memory_order_release);
        kill_reason_ = "";
    }

private:
    std::atomic<bool> killed_;
    const char* kill_reason_;
};

inline KillSwitch& getKillSwitch() noexcept {
    static KillSwitch instance;
    return instance;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ENGINE STATE
// ═══════════════════════════════════════════════════════════════════════════════
enum class EngineState : uint8_t {
    INIT,
    CONNECTING,
    WARMUP,
    RUNNING,
    PAUSED,
    SHUTDOWN
};

inline const char* engine_state_str(EngineState s) noexcept {
    switch (s) {
        case EngineState::INIT: return "INIT";
        case EngineState::CONNECTING: return "CONNECTING";
        case EngineState::WARMUP: return "WARMUP";
        case EngineState::RUNNING: return "RUNNING";
        case EngineState::PAUSED: return "PAUSED";
        case EngineState::SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}

}  // namespace Alpha
