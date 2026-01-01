// ═══════════════════════════════════════════════════════════════════════════════
// cfd_engine/include/config/NewsFilter.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// VERSION: v1.0.0
// OWNER: Jo
//
// PURPOSE:
// Avoid spread blowouts + synthetic repricing around high-impact releases.
// This single module eliminates the "random loss" days on BlackBull.
//
// TIMING:
// - Block new entries: −120s to +120s around event
// - Existing positions: manage exits only (no adds)
//
// EVENTS BLOCKED (High Impact Only):
// - NFP, CPI, FOMC (US)
// - ECB / BoE rate decisions
// - US ISM / Payrolls
// - CPI (EU/UK), GDP (major)
//
// SYMBOL SCOPE:
// - Indices: block US news for US indices; EU news for GER40/UK100
// - FX: block pair-specific currency news
// - Gold: block US CPI/FOMC
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>

namespace chimera {
namespace cfd {
namespace config {

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS EVENT TYPES
// ═══════════════════════════════════════════════════════════════════════════════

enum class NewsImpact : uint8_t {
    LOW = 0,
    MEDIUM,
    HIGH        // Only HIGH impact events trigger blocks
};

enum class NewsCurrency : uint8_t {
    USD = 0,
    EUR,
    GBP,
    JPY,
    AUD,
    CAD,
    CHF,
    NZD,
    CNY,
    ALL         // Affects all markets (e.g., major geopolitical)
};

enum class NewsType : uint8_t {
    // US High Impact
    NFP = 0,            // Non-Farm Payrolls
    CPI_US,             // US Consumer Price Index
    FOMC,               // Federal Reserve rate decision
    ISM_MFG,            // ISM Manufacturing
    ISM_SVC,            // ISM Services
    GDP_US,             // US GDP
    RETAIL_SALES_US,    // US Retail Sales
    JOBLESS_CLAIMS,     // Weekly jobless claims (can be high impact)
    
    // EU High Impact
    ECB_RATE,           // ECB rate decision
    CPI_EU,             // Eurozone CPI
    GDP_EU,             // Eurozone GDP
    
    // UK High Impact
    BOE_RATE,           // Bank of England rate decision
    CPI_UK,             // UK CPI
    GDP_UK,             // UK GDP
    
    // Japan
    BOJ_RATE,           // Bank of Japan rate decision
    
    // Other
    OTHER_HIGH          // Other high-impact event
};

inline const char* news_type_str(NewsType t) noexcept {
    switch (t) {
        case NewsType::NFP:              return "NFP";
        case NewsType::CPI_US:           return "CPI_US";
        case NewsType::FOMC:             return "FOMC";
        case NewsType::ISM_MFG:          return "ISM_MFG";
        case NewsType::ISM_SVC:          return "ISM_SVC";
        case NewsType::GDP_US:           return "GDP_US";
        case NewsType::RETAIL_SALES_US:  return "RETAIL_US";
        case NewsType::JOBLESS_CLAIMS:   return "JOBLESS";
        case NewsType::ECB_RATE:         return "ECB_RATE";
        case NewsType::CPI_EU:           return "CPI_EU";
        case NewsType::GDP_EU:           return "GDP_EU";
        case NewsType::BOE_RATE:         return "BOE_RATE";
        case NewsType::CPI_UK:           return "CPI_UK";
        case NewsType::GDP_UK:           return "GDP_UK";
        case NewsType::BOJ_RATE:         return "BOJ_RATE";
        case NewsType::OTHER_HIGH:       return "OTHER";
        default: return "UNKNOWN";
    }
}

inline const char* currency_str(NewsCurrency c) noexcept {
    switch (c) {
        case NewsCurrency::USD: return "USD";
        case NewsCurrency::EUR: return "EUR";
        case NewsCurrency::GBP: return "GBP";
        case NewsCurrency::JPY: return "JPY";
        case NewsCurrency::AUD: return "AUD";
        case NewsCurrency::CAD: return "CAD";
        case NewsCurrency::CHF: return "CHF";
        case NewsCurrency::NZD: return "NZD";
        case NewsCurrency::CNY: return "CNY";
        case NewsCurrency::ALL: return "ALL";
        default: return "???";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS EVENT STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════

struct NewsEvent {
    NewsType type;
    NewsCurrency currency;
    NewsImpact impact;
    uint64_t timestamp_utc;         // Unix timestamp (seconds)
    int block_before_sec;           // Block this many seconds before (default 120)
    int block_after_sec;            // Block this many seconds after (default 120)
    const char* description;
    
    // Check if current time is within block window
    bool is_blocking(uint64_t now_utc) const noexcept {
        if (impact != NewsImpact::HIGH) return false;
        
        int64_t diff = static_cast<int64_t>(now_utc) - static_cast<int64_t>(timestamp_utc);
        return diff >= -block_before_sec && diff <= block_after_sec;
    }
    
    // Seconds until block starts (negative if already blocking or passed)
    int64_t seconds_until_block(uint64_t now_utc) const noexcept {
        return static_cast<int64_t>(timestamp_utc) - block_before_sec - static_cast<int64_t>(now_utc);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYMBOL AFFECTED CHECK
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Check if a symbol is affected by news for a given currency
 */
inline bool symbol_affected_by_currency(const char* symbol, NewsCurrency currency) noexcept {
    // ALL currency affects everything
    if (currency == NewsCurrency::ALL) return true;
    
    // US indices affected by USD news
    if (currency == NewsCurrency::USD) {
        if (strcmp(symbol, "NAS100") == 0) return true;
        if (strcmp(symbol, "SPX500") == 0) return true;
        if (strcmp(symbol, "US30") == 0) return true;
        if (strcmp(symbol, "XAUUSD") == 0) return true;  // Gold affected by USD
        if (strcmp(symbol, "XAGUSD") == 0) return true;  // Silver affected by USD
        // USD pairs
        if (strstr(symbol, "USD") != nullptr) return true;
    }
    
    // EUR news
    if (currency == NewsCurrency::EUR) {
        if (strcmp(symbol, "GER40") == 0) return true;
        if (strcmp(symbol, "EURUSD") == 0) return true;
        if (strstr(symbol, "EUR") != nullptr) return true;
    }
    
    // GBP news
    if (currency == NewsCurrency::GBP) {
        if (strcmp(symbol, "UK100") == 0) return true;
        if (strcmp(symbol, "GBPUSD") == 0) return true;
        if (strstr(symbol, "GBP") != nullptr) return true;
    }
    
    // JPY news
    if (currency == NewsCurrency::JPY) {
        if (strcmp(symbol, "USDJPY") == 0) return true;
        if (strstr(symbol, "JPY") != nullptr) return true;
    }
    
    // AUD news
    if (currency == NewsCurrency::AUD) {
        if (strcmp(symbol, "AUDUSD") == 0) return true;
        if (strstr(symbol, "AUD") != nullptr) return true;
    }
    
    // CAD news
    if (currency == NewsCurrency::CAD) {
        if (strcmp(symbol, "USDCAD") == 0) return true;
        if (strstr(symbol, "CAD") != nullptr) return true;
    }
    
    // CHF news
    if (currency == NewsCurrency::CHF) {
        if (strcmp(symbol, "USDCHF") == 0) return true;
        if (strstr(symbol, "CHF") != nullptr) return true;
    }
    
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS FILTER RESULT
// ═══════════════════════════════════════════════════════════════════════════════

struct NewsFilterResult {
    bool blocked;                   // Is trading blocked?
    const NewsEvent* blocking_event;// Event causing block (nullptr if not blocked)
    int seconds_until_clear;        // Seconds until block clears (0 if not blocked)
    const char* reason;             // Human-readable reason
    
    explicit operator bool() const noexcept { return !blocked; }  // true = allowed
};

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS CALENDAR
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * NewsCalendar - Manages scheduled high-impact events
 * 
 * USAGE:
 *   1. Load events at startup (from file, API, or hardcoded)
 *   2. Call check_news_filter() before each trade entry
 *   3. Reload events daily or weekly
 */
class NewsCalendar {
public:
    static constexpr int DEFAULT_BLOCK_BEFORE_SEC = 120;
    static constexpr int DEFAULT_BLOCK_AFTER_SEC = 120;
    
    NewsCalendar() = default;
    
    // ═══════════════════════════════════════════════════════════════════════
    // EVENT MANAGEMENT
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Add a high-impact event
     */
    void add_event(
        NewsType type,
        NewsCurrency currency,
        uint64_t timestamp_utc,
        const char* description = nullptr,
        int block_before = DEFAULT_BLOCK_BEFORE_SEC,
        int block_after = DEFAULT_BLOCK_AFTER_SEC
    ) {
        NewsEvent event;
        event.type = type;
        event.currency = currency;
        event.impact = NewsImpact::HIGH;
        event.timestamp_utc = timestamp_utc;
        event.block_before_sec = block_before;
        event.block_after_sec = block_after;
        event.description = description;
        
        events_.push_back(event);
        
        // Keep sorted by timestamp
        std::sort(events_.begin(), events_.end(), 
            [](const NewsEvent& a, const NewsEvent& b) {
                return a.timestamp_utc < b.timestamp_utc;
            });
    }
    
    /**
     * Clear all events (for reload)
     */
    void clear() {
        events_.clear();
    }
    
    /**
     * Remove past events (housekeeping)
     */
    void prune_past_events(uint64_t now_utc) {
        // Keep events that are still within block window or future
        events_.erase(
            std::remove_if(events_.begin(), events_.end(),
                [now_utc](const NewsEvent& e) {
                    return (now_utc > e.timestamp_utc + e.block_after_sec + 60);
                }),
            events_.end()
        );
    }
    
    /**
     * Get count of loaded events
     */
    size_t event_count() const noexcept { return events_.size(); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // NEWS FILTER CHECK
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Check if trading is blocked for a symbol due to news
     * 
     * @param symbol Symbol to check (e.g., "NAS100", "EURUSD")
     * @param now_utc Current Unix timestamp in seconds
     * @return NewsFilterResult with block status and details
     */
    NewsFilterResult check(const char* symbol, uint64_t now_utc) const noexcept {
        NewsFilterResult result;
        result.blocked = false;
        result.blocking_event = nullptr;
        result.seconds_until_clear = 0;
        result.reason = nullptr;
        
        for (const auto& event : events_) {
            // Skip if event doesn't affect this symbol
            if (!symbol_affected_by_currency(symbol, event.currency)) {
                continue;
            }
            
            // Check if we're in the block window
            if (event.is_blocking(now_utc)) {
                result.blocked = true;
                result.blocking_event = &event;
                
                // Calculate time until block clears
                int64_t event_end = event.timestamp_utc + event.block_after_sec;
                result.seconds_until_clear = static_cast<int>(event_end - now_utc);
                if (result.seconds_until_clear < 0) result.seconds_until_clear = 0;
                
                result.reason = news_type_str(event.type);
                return result;
            }
        }
        
        return result;
    }
    
    /**
     * Check using current system time
     */
    NewsFilterResult check_now(const char* symbol) const noexcept {
        return check(symbol, static_cast<uint64_t>(time(nullptr)));
    }
    
    /**
     * Get next event affecting a symbol
     */
    const NewsEvent* get_next_event(const char* symbol, uint64_t now_utc) const noexcept {
        for (const auto& event : events_) {
            if (event.timestamp_utc > now_utc && 
                symbol_affected_by_currency(symbol, event.currency)) {
                return &event;
            }
        }
        return nullptr;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONVENIENCE: ADD COMMON RECURRING EVENTS
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Add NFP (typically first Friday of month, 13:30 UTC)
     */
    void add_nfp(uint64_t timestamp_utc) {
        add_event(NewsType::NFP, NewsCurrency::USD, timestamp_utc, 
                  "Non-Farm Payrolls", 180, 180);  // Wider window for NFP
    }
    
    /**
     * Add FOMC (decision at 19:00 UTC, press conference 19:30 UTC)
     */
    void add_fomc(uint64_t timestamp_utc) {
        add_event(NewsType::FOMC, NewsCurrency::USD, timestamp_utc,
                  "FOMC Rate Decision", 120, 300);  // Extended after for press conf
    }
    
    /**
     * Add US CPI (typically 13:30 UTC)
     */
    void add_us_cpi(uint64_t timestamp_utc) {
        add_event(NewsType::CPI_US, NewsCurrency::USD, timestamp_utc,
                  "US CPI", 120, 180);
    }
    
    /**
     * Add ECB rate decision
     */
    void add_ecb(uint64_t timestamp_utc) {
        add_event(NewsType::ECB_RATE, NewsCurrency::EUR, timestamp_utc,
                  "ECB Rate Decision", 120, 300);
    }
    
    /**
     * Add BoE rate decision
     */
    void add_boe(uint64_t timestamp_utc) {
        add_event(NewsType::BOE_RATE, NewsCurrency::GBP, timestamp_utc,
                  "BoE Rate Decision", 120, 240);
    }

private:
    std::vector<NewsEvent> events_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE (Optional - for simple integration)
// ═══════════════════════════════════════════════════════════════════════════════

inline NewsCalendar& get_news_calendar() {
    static NewsCalendar instance;
    return instance;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE FUNCTION
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Quick check if high-impact news blocks trading
 * 
 * Pseudologic:
 *   if high_impact_news(symbol, now ± 120s):
 *       block_new_entries()
 */
inline bool is_news_blocked(const char* symbol, uint64_t now_utc = 0) {
    if (now_utc == 0) {
        now_utc = static_cast<uint64_t>(time(nullptr));
    }
    return get_news_calendar().check(symbol, now_utc).blocked;
}

} // namespace config
} // namespace cfd
} // namespace chimera
