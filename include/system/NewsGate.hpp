// ═══════════════════════════════════════════════════════════════════════════════
// include/system/NewsGate.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: NEWS-AWARE HARD HALTS
//
// PURPOSE: Hard halt trading around high-impact news events.
// This is NON-NEGOTIABLE in production.
//
// WHY THIS MATTERS:
// There are moments when:
// - Liquidity lies
// - Execution lies  
// - Models lie
//
// Institutions do NOT "trade carefully" during news.
// They HALT. Period.
//
// EVENTS COVERED:
// - CPI
// - FOMC
// - NFP
// - GDP
// - Interest rate decisions
// - Central bank speeches
// - Geopolitical events (manually added)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <ctime>
#include <cstring>
#include <algorithm>

namespace Chimera {
namespace System {

// ─────────────────────────────────────────────────────────────────────────────
// News Event Types
// ─────────────────────────────────────────────────────────────────────────────
enum class NewsEventType : uint8_t {
    UNKNOWN       = 0,
    CPI           = 1,   // Consumer Price Index
    NFP           = 2,   // Non-Farm Payrolls
    FOMC          = 3,   // Federal Reserve decision
    GDP           = 4,   // Gross Domestic Product
    RETAIL_SALES  = 5,   // Retail sales data
    ISM           = 6,   // Manufacturing/Services PMI
    JOBS          = 7,   // Jobless claims
    ECB           = 8,   // European Central Bank
    BOE           = 9,   // Bank of England
    BOJ           = 10,  // Bank of Japan
    EARNINGS      = 11,  // Major earnings (AAPL, NVDA, etc)
    GEOPOLITICAL  = 12,  // Manual override for events
    CUSTOM        = 13   // User-defined
};

inline const char* newsEventTypeStr(NewsEventType t) {
    switch (t) {
        case NewsEventType::UNKNOWN:       return "UNKNOWN";
        case NewsEventType::CPI:           return "CPI";
        case NewsEventType::NFP:           return "NFP";
        case NewsEventType::FOMC:          return "FOMC";
        case NewsEventType::GDP:           return "GDP";
        case NewsEventType::RETAIL_SALES:  return "RETAIL";
        case NewsEventType::ISM:           return "ISM";
        case NewsEventType::JOBS:          return "JOBS";
        case NewsEventType::ECB:           return "ECB";
        case NewsEventType::BOE:           return "BOE";
        case NewsEventType::BOJ:           return "BOJ";
        case NewsEventType::EARNINGS:      return "EARNINGS";
        case NewsEventType::GEOPOLITICAL:  return "GEO";
        case NewsEventType::CUSTOM:        return "CUSTOM";
        default: return "???";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// News Window - Time range for halt
// ─────────────────────────────────────────────────────────────────────────────
struct NewsWindow {
    NewsEventType type = NewsEventType::UNKNOWN;
    time_t start = 0;                  // UTC timestamp
    time_t end = 0;                    // UTC timestamp
    char description[64] = {0};        // Human-readable
    
    bool contains(time_t t) const {
        return t >= start && t <= end;
    }
    
    int minutesUntil(time_t t) const {
        if (t >= start) return 0;
        return static_cast<int>((start - t) / 60);
    }
    
    int minutesRemaining(time_t t) const {
        if (t >= end) return 0;
        return static_cast<int>((end - t) / 60);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// News Gate Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct NewsGateConfig {
    int pre_event_minutes = 15;       // Halt X minutes before
    int post_event_minutes = 10;      // Halt X minutes after
    
    // Per-event overrides (major events get longer windows)
    int fomc_pre_minutes = 30;
    int fomc_post_minutes = 60;
    int nfp_pre_minutes = 20;
    int nfp_post_minutes = 30;
    int cpi_pre_minutes = 15;
    int cpi_post_minutes = 20;
    
    // Symbols affected (empty = all)
    bool affects_crypto = true;
    bool affects_indices = true;
    bool affects_metals = true;
    bool affects_forex = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// News Calendar - Stores upcoming events
// ─────────────────────────────────────────────────────────────────────────────
class NewsCalendar {
public:
    static constexpr size_t MAX_EVENTS = 100;
    
    // Add an event (returns false if calendar full)
    bool addEvent(NewsEventType type, time_t event_time, 
                  const char* description = nullptr) {
        if (count_ >= MAX_EVENTS) return false;
        
        NewsWindow& w = events_[count_];
        w.type = type;
        
        // Calculate window based on event type
        int pre_min = config_.pre_event_minutes;
        int post_min = config_.post_event_minutes;
        
        switch (type) {
            case NewsEventType::FOMC:
                pre_min = config_.fomc_pre_minutes;
                post_min = config_.fomc_post_minutes;
                break;
            case NewsEventType::NFP:
                pre_min = config_.nfp_pre_minutes;
                post_min = config_.nfp_post_minutes;
                break;
            case NewsEventType::CPI:
                pre_min = config_.cpi_pre_minutes;
                post_min = config_.cpi_post_minutes;
                break;
            default:
                break;
        }
        
        w.start = event_time - (pre_min * 60);
        w.end = event_time + (post_min * 60);
        
        if (description) {
            strncpy(w.description, description, sizeof(w.description) - 1);
        } else {
            strncpy(w.description, newsEventTypeStr(type), sizeof(w.description) - 1);
        }
        
        count_++;
        return true;
    }
    
    // Add event with explicit window
    bool addEventWindow(NewsEventType type, time_t start, time_t end,
                        const char* description = nullptr) {
        if (count_ >= MAX_EVENTS) return false;
        
        NewsWindow& w = events_[count_];
        w.type = type;
        w.start = start;
        w.end = end;
        
        if (description) {
            strncpy(w.description, description, sizeof(w.description) - 1);
        }
        
        count_++;
        return true;
    }
    
    // Check if we're in a news halt
    bool isHaltActive(time_t now = 0) const {
        if (now == 0) now = std::time(nullptr);
        
        for (size_t i = 0; i < count_; i++) {
            if (events_[i].contains(now)) {
                return true;
            }
        }
        return false;
    }
    
    // Get current active event (if any)
    const NewsWindow* getActiveEvent(time_t now = 0) const {
        if (now == 0) now = std::time(nullptr);
        
        for (size_t i = 0; i < count_; i++) {
            if (events_[i].contains(now)) {
                return &events_[i];
            }
        }
        return nullptr;
    }
    
    // Get next upcoming event
    const NewsWindow* getNextEvent(time_t now = 0) const {
        if (now == 0) now = std::time(nullptr);
        
        const NewsWindow* next = nullptr;
        for (size_t i = 0; i < count_; i++) {
            if (events_[i].start > now) {
                if (!next || events_[i].start < next->start) {
                    next = &events_[i];
                }
            }
        }
        return next;
    }
    
    // Minutes until next halt (0 if in halt, -1 if none upcoming)
    int minutesUntilNextHalt(time_t now = 0) const {
        if (now == 0) now = std::time(nullptr);
        
        if (isHaltActive(now)) return 0;
        
        const NewsWindow* next = getNextEvent(now);
        if (!next) return -1;
        
        return next->minutesUntil(now);
    }
    
    // Clean up past events
    void prunePastEvents(time_t now = 0) {
        if (now == 0) now = std::time(nullptr);
        
        size_t write = 0;
        for (size_t read = 0; read < count_; read++) {
            if (events_[read].end > now) {
                if (write != read) {
                    events_[write] = events_[read];
                }
                write++;
            }
        }
        count_ = write;
    }
    
    // Accessors
    size_t count() const { return count_; }
    NewsGateConfig& config() { return config_; }
    const NewsGateConfig& config() const { return config_; }
    
    void clear() { count_ = 0; }
    
private:
    NewsWindow events_[MAX_EVENTS];
    size_t count_ = 0;
    NewsGateConfig config_;
};

// ─────────────────────────────────────────────────────────────────────────────
// News Gate - The actual gate check
// ─────────────────────────────────────────────────────────────────────────────
struct NewsGateResult {
    bool halt_active = false;
    const NewsWindow* event = nullptr;
    int minutes_remaining = 0;
    int minutes_until_next = -1;
    const char* reason = "CLEAR";
};

class NewsGate {
public:
    NewsGateResult check(time_t now = 0) const {
        if (now == 0) now = std::time(nullptr);
        
        NewsGateResult result;
        
        // Check if in halt
        const NewsWindow* active = calendar_.getActiveEvent(now);
        if (active) {
            result.halt_active = true;
            result.event = active;
            result.minutes_remaining = active->minutesRemaining(now);
            result.reason = active->description;
        } else {
            result.halt_active = false;
            result.minutes_until_next = calendar_.minutesUntilNextHalt(now);
            result.reason = "CLEAR";
        }
        
        return result;
    }
    
    // Quick check for hot path
    bool isHaltActive(time_t now = 0) const {
        return calendar_.isHaltActive(now);
    }
    
    NewsCalendar& calendar() { return calendar_; }
    const NewsCalendar& calendar() const { return calendar_; }
    
private:
    NewsCalendar calendar_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global News Gate
// ─────────────────────────────────────────────────────────────────────────────
inline NewsGate& getNewsGate() {
    static NewsGate gate;
    return gate;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Add common weekly events
// ─────────────────────────────────────────────────────────────────────────────
inline void addWeeklyJoblessClaims(NewsCalendar& cal, int year, int month) {
    // Jobless claims are every Thursday at 8:30 AM ET (12:30 UTC)
    // This is a simplified version - real implementation would use proper calendar
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 12;
    t.tm_min = 30;
    
    // Find first Thursday
    time_t first = mktime(&t);
    struct tm* tm = gmtime(&first);
    while (tm->tm_wday != 4) {  // 4 = Thursday
        first += 86400;
        tm = gmtime(&first);
    }
    
    // Add all Thursdays in month
    for (int week = 0; week < 5; week++) {
        time_t event_time = first + (week * 7 * 86400);
        tm = gmtime(&event_time);
        if (tm->tm_mon == month - 1) {
            cal.addEvent(NewsEventType::JOBS, event_time, "Jobless Claims");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Parse event from string (for config files)
// ─────────────────────────────────────────────────────────────────────────────
inline NewsEventType parseNewsEventType(const char* str) {
    if (strcmp(str, "CPI") == 0) return NewsEventType::CPI;
    if (strcmp(str, "NFP") == 0) return NewsEventType::NFP;
    if (strcmp(str, "FOMC") == 0) return NewsEventType::FOMC;
    if (strcmp(str, "GDP") == 0) return NewsEventType::GDP;
    if (strcmp(str, "RETAIL") == 0) return NewsEventType::RETAIL_SALES;
    if (strcmp(str, "ISM") == 0) return NewsEventType::ISM;
    if (strcmp(str, "JOBS") == 0) return NewsEventType::JOBS;
    if (strcmp(str, "ECB") == 0) return NewsEventType::ECB;
    if (strcmp(str, "BOE") == 0) return NewsEventType::BOE;
    if (strcmp(str, "BOJ") == 0) return NewsEventType::BOJ;
    if (strcmp(str, "EARNINGS") == 0) return NewsEventType::EARNINGS;
    if (strcmp(str, "GEO") == 0) return NewsEventType::GEOPOLITICAL;
    return NewsEventType::CUSTOM;
}

} // namespace System
} // namespace Chimera
