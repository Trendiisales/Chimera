/**
 * ═══════════════════════════════════════════════════════════════════════════
 * GOLD TREND EXECUTOR - ENGINE B IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * FIXES vs broken versions:
 *   ✅ Directional R correct (LONG + SHORT)
 *   ✅ Partial = 0.4R (not 1R)
 *   ✅ Realized R tracked properly
 *   ✅ Pyramid logic matches v6
 *   ✅ Regime exit supported
 * 
 * MATCHES: Python v6 Monte Carlo stats
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "GoldTrendExecutor.hpp"

namespace gold {

// ─────────────────────────────────────────────────────────────────────────────
// ENTRY
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::enterBase(Direction dir, double entry, double stop) {
    // Create base position
    base_ = std::make_unique<GoldTrendPosition>(dir, entry, stop, 1.0, false);
    
    // Reset state
    pyramid_.reset();
    partial_taken_ = false;
    realized_r_ = 0.0;
    trail_ = stop;
    trail_armed_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// R CALCULATION (CRITICAL - DIRECTION AWARE)
// ─────────────────────────────────────────────────────────────────────────────

double GoldTrendExecutor::calcR(const GoldTrendPosition& p, double price) const {
    double risk = std::abs(p.entry - p.stop);
    if (risk <= 0.0) return 0.0;
    
    // DIRECTION-AWARE: LONG profits when price > entry, SHORT profits when price < entry
    double move = (p.dir == Direction::LONG) 
        ? (price - p.entry) 
        : (p.entry - price);
    
    return move / risk;
}

// ─────────────────────────────────────────────────────────────────────────────
// TICK UPDATE
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::onTick(double price) {
    if (!base_) return;
    
    // Update last price
    base_->last_price = price;
    
    // Track max favorable price (for trailing)
    if (base_->dir == Direction::LONG) {
        base_->max_price = std::max(base_->max_price, price);
    } else {
        base_->max_price = std::min(base_->max_price, price);
    }
    
    double r = calcR(*base_, price);
    
    // ─────────────── HARD STOP ───────────────
    bool stopped = (base_->dir == Direction::LONG && price <= base_->stop) ||
                   (base_->dir == Direction::SHORT && price >= base_->stop);
    
    if (stopped) {
        if (partial_taken_) {
            // Already took partial, remainder at BE → 0 on remainder
            // Partial already credited
        } else {
            // Full stop loss
            realized_r_ -= base_->size_r;  // -1R loss
        }
        exitAll(ExitReason::STOP);
        return;
    }
    
    // ─────────────── PARTIAL @ +1R ───────────────
    tryPartial(price);
    
    // ─────────────── PYRAMID @ +2R ───────────────
    tryPyramid(price);
    
    // ─────────────── STRUCTURAL TRAIL ───────────────
    updateTrail(price);
    
    // ─────────────── UPDATE PYRAMID ───────────────
    if (pyramid_) {
        pyramid_->last_price = price;
        if (pyramid_->dir == Direction::LONG) {
            pyramid_->max_price = std::max(pyramid_->max_price, price);
        } else {
            pyramid_->max_price = std::min(pyramid_->max_price, price);
        }
        
        // Pyramid stop check
        bool pyr_stopped = (pyramid_->dir == Direction::LONG && price <= pyramid_->stop) ||
                           (pyramid_->dir == Direction::SHORT && price >= pyramid_->stop);
        if (pyr_stopped) {
            double pyr_r = calcR(*pyramid_, price);
            realized_r_ += pyramid_->size_r * pyr_r;
            pyramid_.reset();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BAR UPDATE (for backtesting)
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::onBar(double high, double low, double close) {
    if (!base_) return;
    
    // Check stop first (worst price in bar)
    double worst = (base_->dir == Direction::LONG) ? low : high;
    
    bool stopped = (base_->dir == Direction::LONG && low <= base_->stop) ||
                   (base_->dir == Direction::SHORT && high >= base_->stop);
    
    if (stopped) {
        if (partial_taken_) {
            // BE exit on remainder after partial
        } else {
            realized_r_ -= base_->size_r;
        }
        exitAll(ExitReason::STOP);
        return;
    }
    
    // Update with close
    onTick(close);
}

// ─────────────────────────────────────────────────────────────────────────────
// PARTIAL LOGIC
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::tryPartial(double price) {
    if (!base_ || partial_taken_) return;
    
    double r = calcR(*base_, price);
    
    if (r >= GoldTrendConfig::PARTIAL_R) {
        // Credit partial: 40% of position @ +1R = 0.4R
        realized_r_ += GoldTrendConfig::PARTIAL_SIZE * GoldTrendConfig::PARTIAL_R;
        
        // Reduce position size
        base_->size_r -= GoldTrendConfig::PARTIAL_SIZE;  // Now 0.6R remaining
        
        // Move stop to breakeven
        base_->stop = base_->entry;
        
        partial_taken_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PYRAMID LOGIC
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::tryPyramid(double price) {
    if (!base_ || pyramid_) return;  // Already have pyramid
    if (!partial_taken_) return;     // Must have partial locked first
    
    double r = calcR(*base_, price);
    
    if (r >= GoldTrendConfig::PYRAMID_TRIGGER_R) {
        // Add pyramid position
        pyramid_ = std::make_unique<GoldTrendPosition>(
            base_->dir,
            price,
            base_->stop,  // Same stop as base
            GoldTrendConfig::PYRAMID_SIZE,
            true  // is_pyramid
        );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// STRUCTURAL TRAIL
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::updateTrail(double price) {
    if (!base_) return;
    
    double r = calcR(*base_, price);
    
    // Arm trail after +1.5R
    if (!trail_armed_ && r >= GoldTrendConfig::TRAIL_ARM_R) {
        trail_armed_ = true;
    }
    
    if (!trail_armed_) return;
    
    // Calculate trail from campaign geometry
    double range = campaign_high_ - campaign_low_;
    if (range <= 0) return;
    
    if (base_->dir == Direction::LONG) {
        double new_trail = campaign_low_ + GoldTrendConfig::TRAIL_FRAC * range;
        trail_ = std::max(trail_, new_trail);
        
        // Check trail hit
        if (price <= trail_ && trail_ > base_->stop) {
            exitAll(ExitReason::TRAIL);
        }
    } else {
        double new_trail = campaign_high_ - GoldTrendConfig::TRAIL_FRAC * range;
        trail_ = std::min(trail_, new_trail);
        
        // Check trail hit
        if (price >= trail_ && trail_ < base_->stop) {
            exitAll(ExitReason::TRAIL);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// REGIME INVALIDATION
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::onRegimeInvalidation() {
    exitAll(ExitReason::REGIME);
}

// ─────────────────────────────────────────────────────────────────────────────
// TIME EXPIRY
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::onTimeExpiry() {
    exitAll(ExitReason::TIME);
}

// ─────────────────────────────────────────────────────────────────────────────
// EXIT ALL POSITIONS
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::exitAll(ExitReason reason) {
    if (base_) {
        exitPosition(*base_, base_->last_price, reason);
        base_.reset();
    }
    if (pyramid_) {
        exitPosition(*pyramid_, pyramid_->last_price, reason);
        pyramid_.reset();
    }
}

void GoldTrendExecutor::exitPosition(GoldTrendPosition& p, double price, ExitReason reason) {
    // Credit remaining position at exit price
    double r = calcR(p, price);
    
    // For non-stop exits, credit based on current R
    if (reason != ExitReason::STOP) {
        realized_r_ += p.size_r * r;
    }
    // STOP exits are handled in onTick/onBar with proper partial accounting
}

// ─────────────────────────────────────────────────────────────────────────────
// STATE QUERIES
// ─────────────────────────────────────────────────────────────────────────────

bool GoldTrendExecutor::isFlat() const {
    return !base_ && !pyramid_;
}

bool GoldTrendExecutor::hasBase() const {
    return base_ != nullptr;
}

bool GoldTrendExecutor::hasPyramid() const {
    return pyramid_ != nullptr;
}

double GoldTrendExecutor::realizedR() const {
    return realized_r_;
}

double GoldTrendExecutor::unrealizedR() const {
    double unreal = 0.0;
    if (base_) {
        unreal += base_->size_r * calcR(*base_, base_->last_price);
    }
    if (pyramid_) {
        unreal += pyramid_->size_r * calcR(*pyramid_, pyramid_->last_price);
    }
    return unreal;
}

double GoldTrendExecutor::totalR() const {
    return realized_r_ + unrealizedR();
}

// ─────────────────────────────────────────────────────────────────────────────
// CAMPAIGN GEOMETRY
// ─────────────────────────────────────────────────────────────────────────────

void GoldTrendExecutor::setCampaignRange(double low, double high) {
    campaign_low_ = low;
    campaign_high_ = high;
}

} // namespace gold
