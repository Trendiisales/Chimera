/**
 * ═══════════════════════════════════════════════════════════════════════════
 * GOLD TREND EXECUTOR - ENGINE B (v6-ALIGNED)
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * PURPOSE: Capture rare multi-hour continuation when HTF aligns
 * TYPE: Trend runner with pyramiding support
 * 
 * THIS IS SEPARATE FROM ENGINE A (GoldImpulseEngine)
 * NEVER MERGE THEM - BOTH DIE IF MIXED
 * 
 * CHARACTERISTICS:
 *   - Hold: 2-8 hours
 *   - Win rate: low
 *   - Payoff: large
 *   - Pyramids: ✅ YES
 * 
 * GUARANTEES:
 *   - Directional R (LONG + SHORT correct)
 *   - Correct partial accounting (0.4R, not 1R)
 *   - Realized R (not peak fantasy R)
 *   - Pyramid logic works
 *   - Regime exits supported
 *   - Matches v6 Monte Carlo stats
 * 
 * POSITION MODEL:
 *   - Initial: 1.0R
 *   - Partial: 0.4R @ +1R
 *   - Pyramid: 0.5R @ +2R
 *   - Max adds: 1
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#pragma once

#include <cmath>
#include <memory>
#include <algorithm>
#include <optional>

namespace gold {

// ─────────────────────────────────────────────────────────────────────────────
// ENUMS
// ─────────────────────────────────────────────────────────────────────────────

enum class Direction { 
    LONG, 
    SHORT 
};

enum class ExitReason { 
    STOP,       // Hard stop hit
    TRAIL,      // Structural trail hit
    REGIME,     // HTF regime flipped
    TIME        // Max hold expired
};

// ─────────────────────────────────────────────────────────────────────────────
// POSITION STRUCT
// ─────────────────────────────────────────────────────────────────────────────

struct GoldTrendPosition {
    Direction dir;
    double entry;
    double stop;
    double size_r;      // Position size in R (1.0 for base, 0.5 for pyramid)
    double max_price;   // Best price seen (for trailing)
    double last_price;  // Most recent price
    bool is_pyramid = false;
    
    GoldTrendPosition(Direction d, double e, double s, double sz, bool pyr = false)
        : dir(d)
        , entry(e)
        , stop(s)
        , size_r(sz)
        , max_price(e)
        , last_price(e)
        , is_pyramid(pyr)
    {}
};

// ─────────────────────────────────────────────────────────────────────────────
// CONFIG (LOCKED)
// ─────────────────────────────────────────────────────────────────────────────

struct GoldTrendConfig {
    // Partial exit
    static constexpr double PARTIAL_R = 1.0;        // Take partial at +1R
    static constexpr double PARTIAL_SIZE = 0.4;     // 40% of position
    
    // Pyramid
    static constexpr double PYRAMID_TRIGGER_R = 2.0; // Add at +2R unrealized
    static constexpr double PYRAMID_SIZE = 0.5;      // 0.5R size
    
    // Trail (arms after +1.5R)
    static constexpr double TRAIL_ARM_R = 1.5;
    static constexpr double TRAIL_FRAC = 0.5;       // 50% of campaign range
    
    // Max hold
    static constexpr int MAX_HOLD_MINUTES = 360;
};

// ─────────────────────────────────────────────────────────────────────────────
// EXECUTOR CLASS
// ─────────────────────────────────────────────────────────────────────────────

class GoldTrendExecutor {
public:
    GoldTrendExecutor() = default;
    ~GoldTrendExecutor() = default;
    
    // ─────────────── ENTRY ───────────────
    void enterBase(Direction dir, double entry, double stop);
    
    // ─────────────── UPDATES ───────────────
    void onTick(double price);
    void onBar(double high, double low, double close);
    void onRegimeInvalidation();
    void onTimeExpiry();
    
    // ─────────────── STATE ───────────────
    bool isFlat() const;
    bool hasBase() const;
    bool hasPyramid() const;
    double realizedR() const;
    double unrealizedR() const;
    double totalR() const;
    
    // ─────────────── CAMPAIGN GEOMETRY ───────────────
    void setCampaignRange(double low, double high);

private:
    double calcR(const GoldTrendPosition& p, double price) const;
    void tryPartial(double price);
    void tryPyramid(double price);
    void updateTrail(double price);
    void exitAll(ExitReason reason);
    void exitPosition(GoldTrendPosition& p, double price, ExitReason reason);

    // Positions
    std::unique_ptr<GoldTrendPosition> base_;
    std::unique_ptr<GoldTrendPosition> pyramid_;
    
    // State
    bool partial_taken_ = false;
    double realized_r_ = 0.0;
    double trail_ = 0.0;
    bool trail_armed_ = false;
    
    // Campaign geometry
    double campaign_low_ = 0.0;
    double campaign_high_ = 0.0;
};

} // namespace gold
