// ============================================================================
// crypto_engine/include/strategy/MultiStrategyCoordinator.hpp
// ============================================================================
// v6.90: FINAL FIX - Signal normalization + non-cancelling strategies
// ============================================================================
#pragma once

#include <array>
#include <iostream>
#include <cmath>
#include "../regime/RegimeClassifier.hpp"

struct StrategyScore {
    double weight;
    double vote;
};

enum class StrategyIntent : uint8_t {
    FLAT = 0,
    LONG = 1,
    SHORT = 2
};

struct MultiStrategyDecision {
    StrategyIntent intent;
    double confidence;
    uint32_t dominant_strategy;
    double raw_signal;
    double norm_signal;
    
    MultiStrategyDecision() 
        : intent(StrategyIntent::FLAT)
        , confidence(0.0)
        , dominant_strategy(0)
        , raw_signal(0.0)
        , norm_signal(0.0) {}
};

class MultiStrategyCoordinator {
public:
    static constexpr size_t MAX_STRATS = 12;
    static constexpr double NORMALIZED_THRESHOLD = 0.15;
    static constexpr double SIGNAL_NORM_FACTOR = 0.3;

    inline void reset() {
        for (auto& s : scores_) s = {0.0, 0.0};
    }

    inline void submit(size_t idx, double weight, double vote) {
        if (idx < MAX_STRATS) {
            scores_[idx].weight = weight;
            scores_[idx].vote = vote;
        }
    }

    inline double final_intent() const {
        double num = 0.0, den = 0.0;
        for (const auto& s : scores_) {
            num += s.weight * s.vote;
            den += s.weight;
        }
        return den > 0.0 ? num / den : 0.0;
    }
    
    inline MultiStrategyDecision decide(
        const SignalVector& sig,
        MarketRegime regime
    ) {
        reset();
        
        // Strategy 0: OBI momentum (primary directional signal)
        submit(0, regime_weight(regime, 0), sig.obi);
        
        // Strategy 1: Trade flow following (confirms direction)
        submit(1, regime_weight(regime, 1), sig.tfi);
        
        // Strategy 2: DISABLED - was cancelling Strategy 0
        submit(2, 0.0, 0.0);
        
        // Strategy 3: DISABLED
        submit(3, 0.0, 0.0);
        
        double raw = final_intent();
        
        // NORMALIZE SIGNAL TO [-1, +1]
        double norm = raw / SIGNAL_NORM_FACTOR;
        if (norm > 1.0) norm = 1.0;
        if (norm < -1.0) norm = -1.0;
        
        MultiStrategyDecision dec;
        dec.raw_signal = raw;
        dec.norm_signal = norm;
        
        if (norm >= NORMALIZED_THRESHOLD) {
            dec.intent = StrategyIntent::LONG;
            dec.confidence = std::abs(norm);
            dec.dominant_strategy = find_dominant();
        } else if (norm <= -NORMALIZED_THRESHOLD) {
            dec.intent = StrategyIntent::SHORT;
            dec.confidence = std::abs(norm);
            dec.dominant_strategy = find_dominant();
        } else {
            dec.intent = StrategyIntent::FLAT;
            dec.confidence = 0.0;
        }
        
        return dec;
    }

private:
    std::array<StrategyScore, MAX_STRATS> scores_{};
    
    inline double regime_weight(MarketRegime r, size_t strat_idx) const {
        static constexpr double weights[5][2] = {
            {0.4, 0.8},  // MEAN_REVERT
            {1.0, 0.8},  // TREND
            {0.3, 0.3},  // VOLATILE
            {0.0, 0.0},  // ILLIQUID
            {0.7, 0.7}   // NEUTRAL
        };
        if (strat_idx >= 2) return 0.0;
        return weights[static_cast<int>(r)][strat_idx];
    }
    
    inline uint32_t find_dominant() const {
        uint32_t best = 0;
        double best_contrib = 0.0;
        for (size_t i = 0; i < MAX_STRATS; ++i) {
            double contrib = scores_[i].weight * std::abs(scores_[i].vote);
            if (contrib > best_contrib) {
                best_contrib = contrib;
                best = static_cast<uint32_t>(i);
            }
        }
        return best;
    }
};
