// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/SessionWeights.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: TIME-OF-DAY PROFILE WEIGHTING
//
// PURPOSE: Institutions weight signals by trading session.
// Latency, liquidity, and fills vary dramatically by time.
//
// SESSIONS:
// - ASIA: Lower liquidity, wider spreads, reduced activity
// - LONDON: Best liquidity, tightest spreads, highest activity
// - NEW_YORK: High volatility, good liquidity, news-driven
// - OVERLAP: London-NY overlap, maximum opportunity
// - OFF_HOURS: Minimal activity, wider spreads
//
// APPLICATION:
// - Multiply effective edge by session weight
// - Session weighting is not a gate, it's a pricing adjustment
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <ctime>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Trading Session Enumeration
// ─────────────────────────────────────────────────────────────────────────────
enum class TradingSession : uint8_t {
    OFF_HOURS   = 0,  // Weekend or low activity
    ASIA        = 1,  // 00:00-08:00 UTC (Tokyo, Sydney)
    LONDON      = 2,  // 08:00-16:00 UTC (London)
    NEW_YORK    = 3,  // 14:00-21:00 UTC (New York)
    OVERLAP     = 4   // 14:00-16:00 UTC (London-NY overlap)
};

inline const char* sessionStr(TradingSession s) {
    switch (s) {
        case TradingSession::OFF_HOURS: return "OFF_HOURS";
        case TradingSession::ASIA:      return "ASIA";
        case TradingSession::LONDON:    return "LONDON";
        case TradingSession::NEW_YORK:  return "NEW_YORK";
        case TradingSession::OVERLAP:   return "OVERLAP";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Weights Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct SessionWeightsConfig {
    // Edge multipliers by session
    double asia_weight      = 0.70;   // Reduce edge requirements (lower quality)
    double london_weight    = 1.10;   // Best session
    double newyork_weight   = 1.20;   // High volatility = opportunity
    double overlap_weight   = 1.30;   // Maximum liquidity
    double offhours_weight  = 0.50;   // Minimal trading
    
    // Size multipliers by session
    double asia_size_mult     = 0.80;
    double london_size_mult   = 1.00;
    double newyork_size_mult  = 1.00;
    double overlap_size_mult  = 1.20;
    double offhours_size_mult = 0.50;
};

// ─────────────────────────────────────────────────────────────────────────────
// Detect Current Session from UTC hour
// ─────────────────────────────────────────────────────────────────────────────
inline TradingSession detectSession(int utc_hour) {
    // Weekend check would need day-of-week (simplified here)
    
    // Overlap: 14:00-16:00 UTC
    if (utc_hour >= 14 && utc_hour < 16) {
        return TradingSession::OVERLAP;
    }
    
    // London: 08:00-16:00 UTC
    if (utc_hour >= 8 && utc_hour < 16) {
        return TradingSession::LONDON;
    }
    
    // New York: 14:00-21:00 UTC (14-16 already captured as OVERLAP)
    if (utc_hour >= 16 && utc_hour < 21) {
        return TradingSession::NEW_YORK;
    }
    
    // Asia: 00:00-08:00 UTC
    if (utc_hour >= 0 && utc_hour < 8) {
        return TradingSession::ASIA;
    }
    
    // Late evening: 21:00-24:00 UTC
    return TradingSession::OFF_HOURS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get Current UTC Hour
// ─────────────────────────────────────────────────────────────────────────────
inline int getUtcHour() {
    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    return utc ? utc->tm_hour : 12;  // Default to noon if error
}

// ─────────────────────────────────────────────────────────────────────────────
// Get Edge Weight for Current Session
// ─────────────────────────────────────────────────────────────────────────────
inline double getSessionEdgeWeight(const SessionWeightsConfig& cfg = SessionWeightsConfig{}) {
    TradingSession session = detectSession(getUtcHour());
    switch (session) {
        case TradingSession::OVERLAP:   return cfg.overlap_weight;
        case TradingSession::LONDON:    return cfg.london_weight;
        case TradingSession::NEW_YORK:  return cfg.newyork_weight;
        case TradingSession::ASIA:      return cfg.asia_weight;
        case TradingSession::OFF_HOURS: return cfg.offhours_weight;
        default: return 1.0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Get Size Multiplier for Current Session
// ─────────────────────────────────────────────────────────────────────────────
inline double getSessionSizeMultiplier(const SessionWeightsConfig& cfg = SessionWeightsConfig{}) {
    TradingSession session = detectSession(getUtcHour());
    switch (session) {
        case TradingSession::OVERLAP:   return cfg.overlap_size_mult;
        case TradingSession::LONDON:    return cfg.london_size_mult;
        case TradingSession::NEW_YORK:  return cfg.newyork_size_mult;
        case TradingSession::ASIA:      return cfg.asia_size_mult;
        case TradingSession::OFF_HOURS: return cfg.offhours_size_mult;
        default: return 1.0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Snapshot for Logging/GUI
// ─────────────────────────────────────────────────────────────────────────────
struct SessionSnapshot {
    TradingSession session;
    int utc_hour;
    double edge_weight;
    double size_mult;
};

inline SessionSnapshot getCurrentSession(const SessionWeightsConfig& cfg = SessionWeightsConfig{}) {
    int hour = getUtcHour();
    SessionSnapshot snap;
    snap.utc_hour = hour;
    snap.session = detectSession(hour);
    snap.edge_weight = getSessionEdgeWeight(cfg);
    snap.size_mult = getSessionSizeMultiplier(cfg);
    return snap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol-Specific Session Adjustments
// v4.11.0: crypto removed - CFD only
// ─────────────────────────────────────────────────────────────────────────────
// Gold trades best during London and early NY
// Indices trade best during their home session

inline double getSymbolSessionWeight(const char* symbol, TradingSession session) {
    // Gold (XAUUSD): Best during London
    if (symbol[0] == 'X' && symbol[1] == 'A') {
        switch (session) {
            case TradingSession::LONDON:    return 1.20;
            case TradingSession::OVERLAP:   return 1.15;
            case TradingSession::NEW_YORK:  return 1.00;
            case TradingSession::ASIA:      return 0.80;
            default: return 0.60;
        }
    }
    
    // Indices (NAS100, US30): Best during NY
    if (symbol[0] == 'N' || symbol[0] == 'U') {
        switch (session) {
            case TradingSession::NEW_YORK:  return 1.25;
            case TradingSession::OVERLAP:   return 1.10;
            case TradingSession::LONDON:    return 0.90;
            case TradingSession::ASIA:      return 0.70;
            default: return 0.50;
        }
    }
    
    // Default CFD (forex): NY/London have more volume
    switch (session) {
        case TradingSession::NEW_YORK:  return 1.10;
        case TradingSession::OVERLAP:   return 1.15;
        case TradingSession::LONDON:    return 1.05;
        case TradingSession::ASIA:      return 0.90;
        default: return 0.85;
    }
}

} // namespace Execution
} // namespace Chimera
