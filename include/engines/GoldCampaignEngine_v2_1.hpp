// =============================================================================
// GoldCampaignEngine_v2_1.hpp - HARDENED CAMPAIGN DETECTOR
// =============================================================================
// AUDIT FIXES APPLIED:
//   ✅ OBSERVING gated by HTF structural proximity (not random price)
//   ✅ Key level anchored to observe range midpoint (not first close)
//   ✅ Balance logic ATR-based (not brittle price-%)
//   ✅ Hard invalidation on structure break
//
// This engine produces PERMISSION ONLY - never places trades
// =============================================================================
#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace gold {

// =============================================================================
// ENUMS
// =============================================================================
enum class HTFRegime : uint8_t {
    NEUTRAL = 0,
    LONG    = 1,
    SHORT   = 2
};

inline const char* htfRegimeStr(HTFRegime r) {
    switch (r) {
        case HTFRegime::LONG:  return "LONG";
        case HTFRegime::SHORT: return "SHORT";
        default:               return "NEUTRAL";
    }
}

enum class CampaignState : uint8_t {
    INACTIVE    = 0,
    OBSERVING   = 1,
    ACTIVE      = 2,
    INVALIDATED = 3
};

inline const char* campaignStateStr(CampaignState s) {
    switch (s) {
        case CampaignState::OBSERVING:   return "OBSERVING";
        case CampaignState::ACTIVE:      return "ACTIVE";
        case CampaignState::INVALIDATED: return "INVALIDATED";
        default:                         return "INACTIVE";
    }
}

// =============================================================================
// BAR STRUCTS
// =============================================================================
struct H1Bar {
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

struct M5Bar {
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

// =============================================================================
// CAMPAIGN CONTEXT (Output)
// =============================================================================
struct CampaignContext {
    CampaignState state        = CampaignState::INACTIVE;
    HTFRegime     direction    = HTFRegime::NEUTRAL;
    double        key_level    = 0.0;
    double        confidence   = 0.0;
    uint32_t      minutes_live = 0;
    double        observe_high = 0.0;
    double        observe_low  = 0.0;
};

// =============================================================================
// GOLD CAMPAIGN ENGINE v2.1 (HARDENED)
// =============================================================================
class GoldCampaignEngine {
public:
    GoldCampaignEngine() = default;

    // -------------------------------------------------------------------------
    // PUBLIC API
    // -------------------------------------------------------------------------

    void onH1Bar(const H1Bar& bar) {
        h1_.push_back(bar);
        if (h1_.size() > 6) h1_.erase(h1_.begin());
        updateHTFRegime();
        
        // Log regime changes
        static HTFRegime last_regime = HTFRegime::NEUTRAL;
        if (regime_ != last_regime) {
            printf("[GOLD-CAMPAIGN] HTF Regime: %s -> %s\n", 
                   htfRegimeStr(last_regime), htfRegimeStr(regime_));
            last_regime = regime_;
        }
    }

    void onM5Bar(const M5Bar& bar) {
        m5_.push_back(bar);
        if (m5_.size() > 60) m5_.erase(m5_.begin());

        // HTF NEUTRAL = No campaign possible
        if (regime_ == HTFRegime::NEUTRAL) {
            if (campaign_.state != CampaignState::INACTIVE) {
                printf("[GOLD-CAMPAIGN] Reset: HTF went NEUTRAL\n");
            }
            resetCampaign();
            return;
        }

        updateCampaign(bar);
    }

    const CampaignContext& getCampaign() const {
        return campaign_;
    }

    HTFRegime getRegime() const {
        return regime_;
    }
    
    double getObserveHigh() const { return observe_high_; }
    double getObserveLow() const { return observe_low_; }

    void reset() {
        h1_.clear();
        m5_.clear();
        resetCampaign();
        regime_ = HTFRegime::NEUTRAL;
    }

private:
    // -------------------------------------------------------------------------
    // STATE
    // -------------------------------------------------------------------------
    std::vector<H1Bar> h1_;
    std::vector<M5Bar> m5_;

    HTFRegime regime_ = HTFRegime::NEUTRAL;
    CampaignContext campaign_;

    double observe_high_ = 0.0;
    double observe_low_  = 0.0;
    uint32_t observe_minutes_ = 0;
    uint32_t failed_attempts_ = 0;

    // -------------------------------------------------------------------------
    // HTF REGIME DETECTION (H1 Structure)
    // -------------------------------------------------------------------------
    void updateHTFRegime() {
        if (h1_.size() < 4) {
            regime_ = HTFRegime::NEUTRAL;
            return;
        }

        // Look at last 4 H1 bars for structure
        const auto& b0 = h1_[h1_.size() - 4];
        const auto& b1 = h1_[h1_.size() - 3];
        const auto& b2 = h1_[h1_.size() - 2];
        const auto& b3 = h1_[h1_.size() - 1];

        // Higher highs and higher lows = LONG
        bool up = (b3.high > b1.high) && (b2.low > b0.low);
        // Lower lows and lower highs = SHORT
        bool dn = (b3.low < b1.low) && (b2.high < b0.high);

        HTFRegime new_regime = HTFRegime::NEUTRAL;
        if (up) new_regime = HTFRegime::LONG;
        else if (dn) new_regime = HTFRegime::SHORT;

        // Regime change invalidates campaign
        if (new_regime != regime_ && campaign_.state != CampaignState::INACTIVE) {
            printf("[GOLD-CAMPAIGN] Regime change %s -> %s: Campaign invalidated\n",
                   htfRegimeStr(regime_), htfRegimeStr(new_regime));
            resetCampaign();
        }
        
        regime_ = new_regime;
    }

    // -------------------------------------------------------------------------
    // CAMPAIGN STATE MACHINE (M5)
    // -------------------------------------------------------------------------
    void updateCampaign(const M5Bar& bar) {
        switch (campaign_.state) {
            case CampaignState::INACTIVE:
                tryEnterObserving(bar);
                break;
            case CampaignState::OBSERVING:
                updateObserving(bar);
                break;
            case CampaignState::ACTIVE:
                updateActive(bar);
                break;
            case CampaignState::INVALIDATED:
                // Stay invalidated until reset
                break;
        }
    }

    // -------------------------------------------------------------------------
    // OBSERVING ENTRY (AUDIT FIX: HTF proximity gate)
    // -------------------------------------------------------------------------
    void tryEnterObserving(const M5Bar& bar) {
        if (h1_.empty()) return;
        
        // AUDIT FIX: Require proximity to HTF structure
        // Not random price - must be near HTF midpoint
        double htf_mid = (h1_.back().high + h1_.back().low) * 0.5;
        double htf_range = h1_.back().high - h1_.back().low;
        
        // Must be within 0.5 * HTF range of midpoint (or max $15)
        double max_distance = std::min(15.0, htf_range * 0.5);
        if (std::abs(bar.close - htf_mid) > max_distance) {
            return;  // Too far from structure - don't start observing
        }

        observe_high_ = bar.high;
        observe_low_  = bar.low;
        observe_minutes_ = 5;
        failed_attempts_ = 0;

        campaign_.state = CampaignState::OBSERVING;
        campaign_.direction = regime_;
        campaign_.key_level = htf_mid;  // Initial key level = HTF mid
        campaign_.confidence = 0.0;
        campaign_.minutes_live = 0;
        campaign_.observe_high = observe_high_;
        campaign_.observe_low = observe_low_;

        printf("[GOLD-CAMPAIGN] OBSERVING started: dir=%s htf_mid=%.2f\n",
               htfRegimeStr(regime_), htf_mid);
    }

    // -------------------------------------------------------------------------
    // OBSERVING UPDATE
    // -------------------------------------------------------------------------
    void updateObserving(const M5Bar& bar) {
        observe_high_ = std::max(observe_high_, bar.high);
        observe_low_  = std::min(observe_low_, bar.low);
        observe_minutes_ += 5;
        
        campaign_.observe_high = observe_high_;
        campaign_.observe_low = observe_low_;

        // Count failed counter-direction probes
        if (regime_ == HTFRegime::LONG && bar.close < campaign_.key_level) {
            failed_attempts_++;
        }
        if (regime_ == HTFRegime::SHORT && bar.close > campaign_.key_level) {
            failed_attempts_++;
        }

        // AUDIT FIX: ATR-based balance check (not price-%)
        double range = observe_high_ - observe_low_;
        double h1_range = h1Range();
        
        bool balance_ok  = range < (0.6 * h1_range);  // Tight relative to H1
        bool time_ok     = observe_minutes_ >= 45;     // Minimum 45 minutes
        bool failures_ok = failed_attempts_ >= 2;      // At least 2 failed probes

        int confirms = (balance_ok ? 1 : 0) + (time_ok ? 1 : 0) + (failures_ok ? 1 : 0);

        // Need 2 of 3 conditions to activate
        if (confirms >= 2) {
            // AUDIT FIX: Key level = observe range midpoint (structural)
            campaign_.state = CampaignState::ACTIVE;
            campaign_.key_level = (observe_high_ + observe_low_) * 0.5;
            campaign_.confidence = computeConfidence();
            campaign_.minutes_live = 0;

            printf("[GOLD-CAMPAIGN] ACTIVE: key=%.2f conf=%.2f range=%.2f time=%dm fails=%d\n",
                   campaign_.key_level, campaign_.confidence, range,
                   observe_minutes_, failed_attempts_);
        }

        // Check for invalidation
        if (breaksAgainst(bar)) {
            printf("[GOLD-CAMPAIGN] INVALIDATED during OBSERVING: close=%.2f broke %s\n",
                   bar.close, regime_ == HTFRegime::LONG ? "observe_low" : "observe_high");
            campaign_.state = CampaignState::INVALIDATED;
        }
    }

    // -------------------------------------------------------------------------
    // ACTIVE UPDATE
    // -------------------------------------------------------------------------
    void updateActive(const M5Bar& bar) {
        campaign_.minutes_live += 5;

        // Invalidation check
        if (breaksAgainst(bar)) {
            printf("[GOLD-CAMPAIGN] INVALIDATED during ACTIVE: close=%.2f\n", bar.close);
            campaign_.state = CampaignState::INVALIDATED;
            return;
        }

        // Time decay - campaigns don't last forever (max 6 hours)
        if (campaign_.minutes_live > 360) {
            printf("[GOLD-CAMPAIGN] INVALIDATED: Time limit (360 min)\n");
            campaign_.state = CampaignState::INVALIDATED;
        }
    }

    // -------------------------------------------------------------------------
    // INVALIDATION CHECK
    // -------------------------------------------------------------------------
    bool breaksAgainst(const M5Bar& bar) const {
        if (regime_ == HTFRegime::LONG)  return bar.close < observe_low_;
        if (regime_ == HTFRegime::SHORT) return bar.close > observe_high_;
        return true;  // NEUTRAL = always invalid
    }

    // -------------------------------------------------------------------------
    // CONFIDENCE CALCULATION
    // -------------------------------------------------------------------------
    double computeConfidence() const {
        double c = 0.5;
        
        if (failed_attempts_ >= 3) c += 0.2;
        if (observe_minutes_ >= 90) c += 0.2;
        
        double range = observe_high_ - observe_low_;
        if (range < 0.4 * h1Range()) c += 0.1;
        
        return std::min(c, 1.0);
    }

    // -------------------------------------------------------------------------
    // H1 RANGE (for ATR-based comparisons)
    // -------------------------------------------------------------------------
    double h1Range() const {
        if (h1_.empty()) return 20.0;  // Default if no data
        return std::max(1.0, h1_.back().high - h1_.back().low);
    }

    // -------------------------------------------------------------------------
    // RESET
    // -------------------------------------------------------------------------
    void resetCampaign() {
        campaign_ = CampaignContext{};
        observe_high_ = 0.0;
        observe_low_ = 0.0;
        observe_minutes_ = 0;
        failed_attempts_ = 0;
    }
};

} // namespace gold
