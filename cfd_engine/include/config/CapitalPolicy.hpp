// ═══════════════════════════════════════════════════════════════════════════════
// cfd_engine/include/config/CapitalPolicy.hpp - v4.3.4
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 INSTITUTIONAL GRADE
// PURPOSE: A-Book/Tier locking, money windows, overlapping exposure prevention
//
// PRINCIPLES:
// 1. Default = NO TRADE (must pass ALL gates)
// 2. A-Book symbols get full capital, B-Book reduced, C-Book blocked
// 3. Money windows only (London Open, London-NY, NY Open)
// 4. No overlapping index exposure (NAS100 + US30 same direction = blocked)
// 5. Scale-up only after +0.5R and risk-free stop
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#include <time.h>
#define gmtime_safe(timer, result) gmtime_s(result, timer)
#else
#define gmtime_safe(timer, result) gmtime_r(timer, result)
#endif

namespace chimera {
namespace cfd {
namespace policy {

// ═══════════════════════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════════════════════

enum class SymbolTier : uint8_t {
    TIER_A = 0,  // Full capital (core earners)
    TIER_B = 1,  // Reduced capital (conditional)
    TIER_C = 2   // NEVER trades
};

enum class SessionWindow : uint8_t {
    LONDON_OPEN = 0,   // 07:00-09:00 UTC
    LONDON_NY = 1,     // 12:00-14:00 UTC (overlap)
    NY_OPEN = 2,       // 13:30-15:30 UTC
    OTHER = 3          // BLOCKED
};

enum class BlockReason : uint8_t {
    NONE = 0,
    TIER_RESTRICTED,
    SESSION_INVALID,
    SPREAD_WIDE,
    REGIME_MISMATCH,
    EDGE_TOO_WEAK,
    CHOP_DETECTED,
    OVERLAPPING_EXPOSURE,
    DAILY_RISK_LIMIT,
    MAX_POSITIONS
};

inline const char* block_reason_str(BlockReason r) noexcept {
    switch (r) {
        case BlockReason::NONE: return "NONE";
        case BlockReason::TIER_RESTRICTED: return "TIER_RESTRICTED";
        case BlockReason::SESSION_INVALID: return "SESSION_INVALID";
        case BlockReason::SPREAD_WIDE: return "SPREAD_WIDE";
        case BlockReason::REGIME_MISMATCH: return "REGIME_MISMATCH";
        case BlockReason::EDGE_TOO_WEAK: return "EDGE_TOO_WEAK";
        case BlockReason::CHOP_DETECTED: return "CHOP_DETECTED";
        case BlockReason::OVERLAPPING_EXPOSURE: return "OVERLAPPING_EXPOSURE";
        case BlockReason::DAILY_RISK_LIMIT: return "DAILY_RISK_LIMIT";
        case BlockReason::MAX_POSITIONS: return "MAX_POSITIONS";
        default: return "UNKNOWN";
    }
}

inline const char* session_str(SessionWindow s) noexcept {
    switch (s) {
        case SessionWindow::LONDON_OPEN: return "LONDON_OPEN";
        case SessionWindow::LONDON_NY: return "LONDON_NY";
        case SessionWindow::NY_OPEN: return "NY_OPEN";
        case SessionWindow::OTHER: return "OTHER";
        default: return "UNKNOWN";
    }
}

inline const char* tier_str(SymbolTier t) noexcept {
    switch (t) {
        case SymbolTier::TIER_A: return "A";
        case SymbolTier::TIER_B: return "B";
        case SymbolTier::TIER_C: return "C";
        default: return "?";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

struct PositionState {
    std::string symbol;
    int direction;     // +1 long, -1 short
    double open_r;     // current R multiple (unrealized PnL / risk)
    bool risk_free;    // stop at BE or better
};

struct CapitalDecision {
    bool allow_trade = false;
    bool allow_scale_up = false;
    double risk_fraction = 0.0;
    BlockReason block_reason = BlockReason::NONE;
    
    explicit operator bool() const noexcept { return allow_trade; }
};

// ═══════════════════════════════════════════════════════════════════════════════
// CAPITAL POLICY
// ═══════════════════════════════════════════════════════════════════════════════

class CapitalPolicy {
public:
    CapitalPolicy() {
        // 🔒 A-Book (core earners - full capital)
        tier_a_ = {
            "NAS100", "NAS100m",
            "SPX500", "SPX500m",
            "US30", "US30m",
            "XAUUSD", "XAUUSDm"
        };
        
        // ⚠️ B-Book (conditional - reduced capital)
        tier_b_ = {
            "GER40", "GER40m",
            "UK100", "UK100m",
            "EURUSD", "EURUSDm",
            "GBPUSD", "GBPUSDm",
            "USDJPY", "USDJPYm"
        };
        
        // Everything else = Tier C (blocked)
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // SYMBOL TIER
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] SymbolTier symbol_tier(const std::string& symbol) const noexcept {
        if (tier_a_.count(symbol)) return SymbolTier::TIER_A;
        if (tier_b_.count(symbol)) return SymbolTier::TIER_B;
        return SymbolTier::TIER_C;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // SESSION WINDOW (UTC)
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] SessionWindow current_session(std::time_t utc_now) const noexcept {
        std::tm tm{};
        gmtime_safe(&utc_now, &tm);
        int hour = tm.tm_hour;
        int min = tm.tm_min;
        int minutes = hour * 60 + min;
        
        // London Open: 07:00 - 09:00 UTC
        if (minutes >= 7*60 && minutes < 9*60) {
            return SessionWindow::LONDON_OPEN;
        }
        
        // London-NY Overlap: 12:00 - 14:00 UTC
        if (minutes >= 12*60 && minutes < 14*60) {
            return SessionWindow::LONDON_NY;
        }
        
        // NY Open: 13:30 - 15:30 UTC
        if (minutes >= 13*60+30 && minutes < 15*60+30) {
            return SessionWindow::NY_OPEN;
        }
        
        return SessionWindow::OTHER;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // MAIN EVALUATION (Default = NO TRADE)
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] CapitalDecision evaluate(
        const std::string& symbol,
        SessionWindow session,
        double edge_strength,           // 0-2+ (1.0 = minimum acceptable)
        double spread,                  // Current spread
        double spread_limit,            // Max spread for this symbol/session
        bool regime_allowed,            // True if regime is tradeable
        bool chop_detected,             // True if chopping market
        const std::vector<PositionState>& open_positions,
        double daily_r_used,            // How much daily R budget used
        int direction                   // +1 long, -1 short (for overlap check)
    ) const noexcept {
        CapitalDecision d{};
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 1: Tier Lock
        // ════════════════════════════════════════════════════════════════════
        SymbolTier tier = symbol_tier(symbol);
        if (tier == SymbolTier::TIER_C) {
            d.block_reason = BlockReason::TIER_RESTRICTED;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 2: Money Windows Only
        // ════════════════════════════════════════════════════════════════════
        if (session == SessionWindow::OTHER) {
            d.block_reason = BlockReason::SESSION_INVALID;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 3: Spread Discipline
        // ════════════════════════════════════════════════════════════════════
        if (spread > spread_limit) {
            d.block_reason = BlockReason::SPREAD_WIDE;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 4: Regime Mismatch
        // ════════════════════════════════════════════════════════════════════
        if (!regime_allowed) {
            d.block_reason = BlockReason::REGIME_MISMATCH;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 5: Chop Detection
        // ════════════════════════════════════════════════════════════════════
        if (chop_detected) {
            d.block_reason = BlockReason::CHOP_DETECTED;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 6: Edge Strength
        // ════════════════════════════════════════════════════════════════════
        if (edge_strength < 1.0) {
            d.block_reason = BlockReason::EDGE_TOO_WEAK;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 7: Overlapping Index Exposure
        // ════════════════════════════════════════════════════════════════════
        if (overlapping_index_exposure(symbol, direction, open_positions)) {
            d.block_reason = BlockReason::OVERLAPPING_EXPOSURE;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 8: Daily Risk Cap (2.0R max)
        // ════════════════════════════════════════════════════════════════════
        if (daily_r_used >= 2.0) {
            d.block_reason = BlockReason::DAILY_RISK_LIMIT;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // GATE 9: Max Positions (2 concurrent)
        // ════════════════════════════════════════════════════════════════════
        if (open_positions.size() >= 2) {
            d.block_reason = BlockReason::MAX_POSITIONS;
            return d;
        }
        
        // ════════════════════════════════════════════════════════════════════
        // ✅ ALL GATES PASSED - TRADE ALLOWED
        // ════════════════════════════════════════════════════════════════════
        d.allow_trade = true;
        
        // Base risk by tier
        d.risk_fraction = (tier == SymbolTier::TIER_A) ? 0.005 : 0.0025;
        
        // Session multiplier
        switch (session) {
            case SessionWindow::LONDON_OPEN: d.risk_fraction *= 1.4; break;
            case SessionWindow::LONDON_NY:   d.risk_fraction *= 1.2; break;
            case SessionWindow::NY_OPEN:     d.risk_fraction *= 1.6; break;
            default: break;
        }
        
        // Scale-up check (one add only, after +0.5R, must be risk-free)
        for (const auto& pos : open_positions) {
            if (pos.symbol == symbol &&
                pos.open_r >= 0.5 &&
                pos.risk_free &&
                session != SessionWindow::OTHER) {
                d.allow_scale_up = true;
                d.risk_fraction *= 1.5;  // +50% for scale-up
                break;
            }
        }
        
        return d;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // AUDIT LOGGING
    // ─────────────────────────────────────────────────────────────────────────
    static void log_decision(
        const std::string& symbol,
        const CapitalDecision& d,
        SessionWindow session,
        const std::string& note = ""
    ) {
        std::cout << "[CAPITAL-POLICY] " << symbol
                  << " session=" << session_str(session)
                  << " allowed=" << (d.allow_trade ? "YES" : "NO")
                  << " risk=" << (d.risk_fraction * 100.0) << "%"
                  << " reason=" << block_reason_str(d.block_reason);
        if (d.allow_scale_up) std::cout << " [SCALE-UP]";
        if (!note.empty()) std::cout << " note=" << note;
        std::cout << "\n";
        std::cout.flush();
    }

private:
    std::unordered_set<std::string> tier_a_;
    std::unordered_set<std::string> tier_b_;
    
    // ─────────────────────────────────────────────────────────────────────────
    // INDEX OVERLAP CHECK
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_index(const std::string& symbol) const noexcept {
        return symbol.find("NAS100") != std::string::npos ||
               symbol.find("SPX500") != std::string::npos ||
               symbol.find("US30") != std::string::npos ||
               symbol.find("GER40") != std::string::npos ||
               symbol.find("UK100") != std::string::npos;
    }
    
    [[nodiscard]] bool overlapping_index_exposure(
        const std::string& symbol,
        int direction,
        const std::vector<PositionState>& open_positions
    ) const noexcept {
        if (!is_index(symbol)) return false;
        
        for (const auto& pos : open_positions) {
            // Same direction index already open AND not risk-free
            if (is_index(pos.symbol) &&
                pos.direction == direction &&
                !pos.risk_free) {
                return true;  // BLOCKED
            }
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// CAPITAL LADDER (Equity-based scaling)
// ═══════════════════════════════════════════════════════════════════════════════

class CapitalLadder {
public:
    CapitalLadder() {
        // Conservative compounding ladder
        ladder_ = {
            {      0.0, 0.0025 },  // 0.25% base
            {  50000.0, 0.0030 },  // After $50k
            { 100000.0, 0.0035 },  // After $100k
            { 200000.0, 0.0040 },  // After $200k
            { 500000.0, 0.0050 }   // Cap at 0.5%
        };
    }
    
    [[nodiscard]] double base_risk_for_equity(double equity) const noexcept {
        double risk = ladder_.front().base_risk;
        for (const auto& step : ladder_) {
            if (equity >= step.equity_min) {
                risk = step.base_risk;
            } else {
                break;
            }
        }
        return risk;
    }

private:
    struct LadderStep {
        double equity_min;
        double base_risk;
    };
    std::vector<LadderStep> ladder_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE
// ═══════════════════════════════════════════════════════════════════════════════

inline CapitalPolicy& getCapitalPolicy() {
    static CapitalPolicy instance;
    return instance;
}

inline CapitalLadder& getCapitalLadder() {
    static CapitalLadder instance;
    return instance;
}

}  // namespace policy
}  // namespace cfd
}  // namespace chimera
