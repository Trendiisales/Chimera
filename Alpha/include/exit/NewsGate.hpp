// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - News Gate
// ═══════════════════════════════════════════════════════════════════════════════
// DESIGN RULE:
// - High-impact news = extend hold (don't exit winners before expansion)
// - Never add size during news
// - Never open new positions during news window
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <ctime>
#include <cstdint>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS WINDOW CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════

constexpr int NEWS_BLOCK_BEFORE_SEC = 120;   // Block entries 2 min before
constexpr int NEWS_BLOCK_AFTER_SEC = 60;     // Block entries 1 min after
constexpr int NEWS_HOLD_WINDOW_SEC = 300;    // Extended holds 5 min around news

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS DETECTION
// ═══════════════════════════════════════════════════════════════════════════════

inline bool high_impact_news_near(std::time_t utc_now) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &utc_now);
#else
    gmtime_r(&utc_now, &tm);
#endif

    int h = tm.tm_hour;
    int m = tm.tm_min;
    int wday = tm.tm_wday;  // 0=Sunday
    int mday = tm.tm_mday;

    // ═══════════════════════════════════════════════════════════════════════════
    // MAJOR US DATA RELEASES (UTC times)
    // ═══════════════════════════════════════════════════════════════════════════

    // NFP - First Friday of month at 13:30 UTC
    if (wday == 5 && mday <= 7 && h == 13 && m >= 28 && m <= 35)
        return true;

    // CPI/PPI - Mid-month Tuesday/Wednesday at 13:30 UTC
    if ((wday == 2 || wday == 3) && mday >= 10 && mday <= 15 && h == 13 && m >= 28 && m <= 35)
        return true;

    // FOMC Minutes - Wednesday at 19:00 UTC (8 times per year)
    if (wday == 3 && h == 18 && m >= 58)
        return true;
    if (wday == 3 && h == 19 && m <= 5)
        return true;

    // Initial Jobless Claims - Thursday at 13:30 UTC
    if (wday == 4 && h == 13 && m >= 28 && m <= 35)
        return true;

    // ISM Manufacturing/Services - First business day at 15:00 UTC
    if (mday <= 3 && h == 14 && m >= 58)
        return true;
    if (mday <= 3 && h == 15 && m <= 5)
        return true;

    // Retail Sales - Mid-month at 13:30 UTC
    if (mday >= 13 && mday <= 17 && h == 13 && m >= 28 && m <= 35)
        return true;

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// NEWS HOLD EXTENSION
// ═══════════════════════════════════════════════════════════════════════════════
// Require extra R-multiple to exit during news window

inline double news_exit_extension() {
    return 0.25;  // Require +0.25R to exit during news
}

// ═══════════════════════════════════════════════════════════════════════════════
// ENTRY RULES DURING NEWS
// ═══════════════════════════════════════════════════════════════════════════════

inline bool news_blocks_entry(std::time_t utc_now) {
    // Block new entries during high-impact news window
    return high_impact_news_near(utc_now);
}

inline bool news_blocks_scaling(std::time_t utc_now) {
    // Never add size during news
    return high_impact_news_near(utc_now);
}

}  // namespace Alpha
