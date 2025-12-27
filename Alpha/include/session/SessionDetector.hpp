// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Session Detection
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.1.0
// PURPOSE: Identify EXACTLY when each instrument has edge
// UPDATED: Added SessionType enum for exit logic integration
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "core/Types.hpp"
#include <ctime>

#ifdef _WIN32
#define gmtime_safe(timer, result) gmtime_s(result, timer)
#else
#define gmtime_safe(timer, result) gmtime_r(timer, result)
#endif

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// GENERIC SESSION TYPE (for exit logic)
// ═══════════════════════════════════════════════════════════════════════════════
enum class SessionType : uint8_t {
    LONDON_OPEN,      // 07:00-09:00 UTC - Gold peak
    US_DATA,          // 13:30-15:00 UTC - Gold peak, NAS overlap
    CASH_OPEN,        // 13:30-15:30 UTC - NAS peak
    POWER_HOUR,       // 19:00-20:30 UTC - NAS peak
    ASIA,             // 00:00-06:00 UTC - DEFAULT BLOCKED
    LONDON_PM,        // 10:00-12:00 UTC - Secondary
    NY_AFTERNOON,     // 16:00-18:00 UTC - Secondary
    PRE_MARKET,       // 12:00-13:30 UTC - NAS secondary
    MIDDAY,           // 15:30-18:00 UTC - NAS secondary (chop)
    OFF               // No edge - no trading
};

inline const char* session_type_str(SessionType st) noexcept {
    switch (st) {
        case SessionType::LONDON_OPEN:  return "LONDON_OPEN";
        case SessionType::US_DATA:      return "US_DATA";
        case SessionType::CASH_OPEN:    return "CASH_OPEN";
        case SessionType::POWER_HOUR:   return "POWER_HOUR";
        case SessionType::ASIA:         return "ASIA";
        case SessionType::LONDON_PM:    return "LONDON_PM";
        case SessionType::NY_AFTERNOON: return "NY_AFTERNOON";
        case SessionType::PRE_MARKET:   return "PRE_MARKET";
        case SessionType::MIDDAY:       return "MIDDAY";
        case SessionType::OFF:          return "OFF";
    }
    return "UNKNOWN";
}

inline bool is_prime_session(SessionType st) noexcept {
    return st == SessionType::LONDON_OPEN || 
           st == SessionType::US_DATA ||
           st == SessionType::CASH_OPEN ||
           st == SessionType::POWER_HOUR;
}

inline bool is_asia_session(SessionType st) noexcept {
    return st == SessionType::ASIA;
}

// Get current session type from UTC time
inline SessionType get_session_type(std::time_t utc_time) noexcept {
    std::tm tm{};
    gmtime_safe(&utc_time, &tm);
    
    int hour = tm.tm_hour;
    int minute = tm.tm_min;
    int minutes = hour * 60 + minute;
    
    // Asia: 00:00-06:00 UTC
    if (minutes < 6*60)
        return SessionType::ASIA;
    
    // London Open: 07:00-09:00 UTC
    if (minutes >= 7*60 && minutes < 9*60)
        return SessionType::LONDON_OPEN;
    
    // London PM: 10:00-12:00 UTC
    if (minutes >= 10*60 && minutes < 12*60)
        return SessionType::LONDON_PM;
    
    // Pre-Market: 12:00-13:30 UTC
    if (minutes >= 12*60 && minutes < 13*60+30)
        return SessionType::PRE_MARKET;
    
    // US Data / Cash Open: 13:30-15:30 UTC
    if (minutes >= 13*60+30 && minutes < 15*60+30)
        return SessionType::CASH_OPEN;  // Also US_DATA for gold
    
    // Midday: 15:30-18:00 UTC
    if (minutes >= 15*60+30 && minutes < 18*60)
        return SessionType::MIDDAY;
    
    // NY Afternoon: 16:00-18:00 (overlaps midday)
    // Power Hour: 19:00-20:30 UTC
    if (minutes >= 19*60 && minutes < 20*60+30)
        return SessionType::POWER_HOUR;
    
    return SessionType::OFF;
}

inline SessionType current_session_type() noexcept {
    return get_session_type(std::time(nullptr));
}

// ═══════════════════════════════════════════════════════════════════════════════
// INSTRUMENT-SPECIFIC SESSION TYPES
// ═══════════════════════════════════════════════════════════════════════════════
enum class Session : uint8_t {
    // XAUUSD Sessions
    GOLD_LONDON_OPEN,    // 07:00-09:00 UTC - 🔥 PEAK
    GOLD_US_DATA,        // 13:30-15:00 UTC - 🔥 PEAK
    GOLD_ASIA,           // 00:00-03:00 UTC - ⚠️ SECONDARY
    GOLD_LONDON_PM,      // 10:00-12:00 UTC - ⚠️ SECONDARY
    GOLD_NY_AFTERNOON,   // 16:00-18:00 UTC - ⚠️ SECONDARY
    
    // NAS100 Sessions
    NAS_CASH_OPEN,       // 13:30-15:30 UTC - 🔥 PEAK
    NAS_POWER_HOUR,      // 19:00-20:30 UTC - 🔥 PEAK
    NAS_PRE_MARKET,      // 12:00-13:30 UTC - ⚠️ SECONDARY
    NAS_MIDDAY,          // 15:30-18:00 UTC - ⚠️ SECONDARY (careful!)
    
    OFF
};

inline const char* session_str(Session s) noexcept {
    switch (s) {
        case Session::GOLD_LONDON_OPEN: return "GOLD_LONDON";
        case Session::GOLD_US_DATA: return "GOLD_US_DATA";
        case Session::NAS_CASH_OPEN: return "NAS_OPEN";
        case Session::NAS_POWER_HOUR: return "NAS_POWER";
        case Session::GOLD_ASIA: return "GOLD_ASIA";
        case Session::GOLD_LONDON_PM: return "GOLD_PM";
        case Session::GOLD_NY_AFTERNOON: return "GOLD_NY_PM";
        case Session::NAS_PRE_MARKET: return "NAS_PRE";
        case Session::NAS_MIDDAY: return "NAS_MIDDAY";
        case Session::OFF: return "OFF";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION INFO
// ═══════════════════════════════════════════════════════════════════════════════
struct SessionInfo {
    Session session = Session::OFF;
    double size_multiplier = 0.0;     // 0 = no trade, 1 = normal, 2+ = aggressive
    double spread_tolerance = 1.0;    // Spread threshold multiplier
    bool is_peak = false;
    bool is_secondary = false;
    int minutes_into_session = 0;
    int minutes_until_end = 0;
    bool near_session_change = false;
    
    [[nodiscard]] bool can_trade() const noexcept {
        return size_multiplier > 0.0 && session != Session::OFF;
    }
    
    [[nodiscard]] bool should_exit_before_end() const noexcept {
        return minutes_until_end <= 3;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION DETECTOR
// ═══════════════════════════════════════════════════════════════════════════════
class SessionDetector {
public:
    [[nodiscard]] SessionInfo detect(Instrument inst) const noexcept {
        std::time_t utc = std::time(nullptr);
        return detect(inst, utc);
    }
    
    [[nodiscard]] SessionInfo detect(Instrument inst, std::time_t utc_time) const noexcept {
        std::tm tm{};
        gmtime_safe(&utc_time, &tm);
        
        int hour = tm.tm_hour;
        int minute = tm.tm_min;
        int dow = tm.tm_wday;
        int minutes = hour * 60 + minute;
        
        // Weekend = OFF
        if (dow == 0 || dow == 6) return make_off();
        
        // Friday after 21:00 = OFF
        if (dow == 5 && minutes >= 21 * 60) return make_off();
        
        // Monday before 00:30 = OFF
        if (dow == 1 && minutes < 30) return make_off();
        
        switch (inst) {
            case Instrument::XAUUSD: return detect_gold(minutes);
            case Instrument::NAS100: return detect_nas(minutes);
            default: return make_off();
        }
    }

private:
    [[nodiscard]] SessionInfo detect_gold(int minutes) const noexcept {
        // 🔥 PEAK: London Open 07:00-09:00 UTC
        if (in_window(minutes, 7*60, 9*60)) {
            return make_peak(Session::GOLD_LONDON_OPEN, minutes - 7*60, 9*60 - minutes, 2.0, 0.85);
        }
        
        // 🔥 PEAK: US Data 13:30-15:00 UTC
        if (in_window(minutes, 13*60+30, 15*60)) {
            return make_peak(Session::GOLD_US_DATA, minutes - (13*60+30), 15*60 - minutes, 2.5, 0.90);
        }
        
        // ⚠️ SECONDARY: Asia 00:00-03:00 UTC
        if (in_window(minutes, 0, 3*60)) {
            return make_secondary(Session::GOLD_ASIA, minutes, 3*60 - minutes, 0.7, 1.3);
        }
        
        // ⚠️ SECONDARY: London PM 10:00-12:00 UTC
        if (in_window(minutes, 10*60, 12*60)) {
            return make_secondary(Session::GOLD_LONDON_PM, minutes - 10*60, 12*60 - minutes, 0.8, 1.0);
        }
        
        // ⚠️ SECONDARY: NY Afternoon 16:00-18:00 UTC
        if (in_window(minutes, 16*60, 18*60)) {
            return make_secondary(Session::GOLD_NY_AFTERNOON, minutes - 16*60, 18*60 - minutes, 0.6, 1.1);
        }
        
        return make_off();
    }
    
    [[nodiscard]] SessionInfo detect_nas(int minutes) const noexcept {
        // 🔥 PEAK: Cash Open 13:30-15:30 UTC
        if (in_window(minutes, 13*60+30, 15*60+30)) {
            return make_peak(Session::NAS_CASH_OPEN, minutes - (13*60+30), 15*60+30 - minutes, 2.5, 0.80);
        }
        
        // 🔥 PEAK: Power Hour 19:00-20:30 UTC
        if (in_window(minutes, 19*60, 20*60+30)) {
            return make_peak(Session::NAS_POWER_HOUR, minutes - 19*60, 20*60+30 - minutes, 2.0, 0.85);
        }
        
        // ⚠️ SECONDARY: Pre-Market 12:00-13:30 UTC
        if (in_window(minutes, 12*60, 13*60+30)) {
            return make_secondary(Session::NAS_PRE_MARKET, minutes - 12*60, 13*60+30 - minutes, 0.6, 1.4);
        }
        
        // ⚠️ SECONDARY: Midday 15:30-18:00 UTC (VERY CAREFUL - lunch chop)
        if (in_window(minutes, 15*60+30, 18*60)) {
            return make_secondary(Session::NAS_MIDDAY, minutes - (15*60+30), 18*60 - minutes, 0.4, 1.0);
        }
        
        return make_off();
    }
    
    [[nodiscard]] static bool in_window(int minutes, int start, int end) noexcept {
        return minutes >= start && minutes < end;
    }
    
    [[nodiscard]] static SessionInfo make_peak(Session s, int into, int until,
                                               double size_mult, double spread_tol) noexcept {
        SessionInfo info;
        info.session = s;
        info.size_multiplier = size_mult;
        info.spread_tolerance = spread_tol;
        info.is_peak = true;
        info.is_secondary = false;
        info.minutes_into_session = into;
        info.minutes_until_end = until;
        info.near_session_change = (into < 5 || until < 5);
        return info;
    }
    
    [[nodiscard]] static SessionInfo make_secondary(Session s, int into, int until,
                                                    double size_mult, double spread_tol) noexcept {
        SessionInfo info;
        info.session = s;
        info.size_multiplier = size_mult;
        info.spread_tolerance = spread_tol;
        info.is_peak = false;
        info.is_secondary = true;
        info.minutes_into_session = into;
        info.minutes_until_end = until;
        info.near_session_change = (into < 5 || until < 5);
        return info;
    }
    
    [[nodiscard]] static SessionInfo make_off() noexcept {
        SessionInfo info;
        info.session = Session::OFF;
        info.size_multiplier = 0.0;
        return info;
    }
};

inline SessionDetector& getSessionDetector() noexcept {
    static SessionDetector instance;
    return instance;
}

inline SessionInfo current_session(Instrument inst) noexcept {
    return getSessionDetector().detect(inst);
}

inline bool can_trade_now(Instrument inst) noexcept {
    return current_session(inst).can_trade();
}

}  // namespace Alpha
