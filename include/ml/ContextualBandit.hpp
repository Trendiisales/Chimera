// =============================================================================
// ContextualBandit.hpp - Thompson Sampling for Aggression Control
// =============================================================================
// PURPOSE: Learn optimal aggression level per market state
// DESIGN:
//   - Beta distribution per state (alpha, beta parameters)
//   - Thompson sampling for exploration vs exploitation
//   - Updates on trade outcomes (R > 0 = success)
//   - Safe bounds on multiplier output
//
// WHY BANDIT, NOT FULL RL:
//   - Simple, interpretable, stable
//   - No price prediction, just "how aggressive given this state"
//   - Converges quickly with few samples
//   - Automatic exploration decay
//
// USAGE:
//   ContextualBandit bandit;
//   
//   // Before trade:
//   double mult = bandit.chooseMultiplier(state, expected_R);
//   size *= mult;
//   
//   // After trade close:
//   bandit.update(state, realized_R);
//
// MATH:
//   Each state has Beta(α, β) distribution
//   - α increases on wins (reward > 0)
//   - β increases on losses (reward < 0)
//   - Sample from Beta → scale to multiplier
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <array>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Chimera {
namespace ML {

// =============================================================================
// Bandit Configuration
// =============================================================================
struct BanditConfig {
    // Output bounds
    double min_multiplier = 0.25;
    double max_multiplier = 2.0;
    
    // Prior parameters (Beta distribution)
    double initial_alpha = 1.0;  // Prior successes
    double initial_beta = 1.0;   // Prior failures
    
    // Update scaling
    double win_weight = 1.0;     // How much to weight wins
    double loss_weight = 1.0;    // How much to weight losses
    
    // Decay (for adaptivity to regime change)
    double decay_rate = 0.999;   // Applied each update
    double min_alpha = 0.5;      // Don't decay below this
    double min_beta = 0.5;
    
    // Expected R threshold for aggressive mode
    double aggressive_threshold = 0.5;
};

// =============================================================================
// State-Specific Arm
// =============================================================================
struct BanditArm {
    double alpha;
    double beta;
    uint64_t samples;
    double total_reward;
    
    BanditArm() noexcept : alpha(1.0), beta(1.0), samples(0), total_reward(0.0) {}
    
    BanditArm(double a, double b) noexcept 
        : alpha(a), beta(b), samples(0), total_reward(0.0) {}
    
    // Sample from Beta distribution
    double sample(std::mt19937& rng) const noexcept {
        std::gamma_distribution<double> gamma_a(alpha, 1.0);
        std::gamma_distribution<double> gamma_b(beta, 1.0);
        double x = gamma_a(rng);
        double y = gamma_b(rng);
        return x / (x + y + 1e-9);  // Beta sample in [0, 1]
    }
    
    // Update based on outcome
    void update(double reward, double win_weight, double loss_weight) noexcept {
        samples++;
        total_reward += reward;
        
        if (reward > 0) {
            alpha += std::min(reward, 2.0) * win_weight;
        } else {
            beta += std::min(std::fabs(reward), 2.0) * loss_weight;
        }
    }
    
    // Apply decay
    void decay(double rate, double min_a, double min_b) noexcept {
        alpha = std::max(min_a, alpha * rate);
        beta = std::max(min_b, beta * rate);
    }
    
    // Expected value of Beta distribution
    double mean() const noexcept {
        return alpha / (alpha + beta);
    }
    
    // Variance (for diagnostics)
    double variance() const noexcept {
        double ab = alpha + beta;
        return (alpha * beta) / (ab * ab * (ab + 1));
    }
};

// =============================================================================
// Contextual Bandit
// =============================================================================
class ContextualBandit {
public:
    static constexpr size_t NUM_STATES = 4;  // DEAD, TRENDING, RANGING, VOLATILE
    
    ContextualBandit() noexcept : ContextualBandit(BanditConfig()) {}
    
    explicit ContextualBandit(const BanditConfig& config) noexcept
        : config_(config)
        , rng_(std::random_device{}())
    {
        for (auto& arm : arms_) {
            arm = BanditArm(config_.initial_alpha, config_.initial_beta);
        }
    }
    
    // =========================================================================
    // Core API
    // =========================================================================
    
    // Choose multiplier based on state and ML expected R
    double chooseMultiplier(MLMarketState state, double expected_R) noexcept {
        size_t idx = static_cast<size_t>(state);
        if (idx >= NUM_STATES) idx = 0;
        
        // Sample from Beta distribution
        double sample = arms_[idx].sample(rng_);
        
        // Scale sample based on expected R
        double base_mult = config_.min_multiplier + 
                          sample * (config_.max_multiplier - config_.min_multiplier);
        
        // If expected R is high, bias toward aggressive
        if (expected_R > config_.aggressive_threshold) {
            base_mult = std::min(config_.max_multiplier,
                                base_mult * (1.0 + 0.3 * expected_R));
        } else if (expected_R < 0) {
            // Negative expected R = reduce
            base_mult = std::max(config_.min_multiplier,
                                base_mult * 0.5);
        }
        
        return std::clamp(base_mult, config_.min_multiplier, config_.max_multiplier);
    }
    
    // Simpler version without expected_R
    double chooseMultiplier(MLMarketState state) noexcept {
        return chooseMultiplier(state, 0.0);
    }
    
    // Update based on realized R
    void update(MLMarketState state, double reward) noexcept {
        size_t idx = static_cast<size_t>(state);
        if (idx >= NUM_STATES) idx = 0;
        
        arms_[idx].update(reward, config_.win_weight, config_.loss_weight);
        
        // Apply decay to all arms (adaptivity)
        for (auto& arm : arms_) {
            arm.decay(config_.decay_rate, config_.min_alpha, config_.min_beta);
        }
    }
    
    // =========================================================================
    // Bulk Operations
    // =========================================================================
    
    // Reset to priors
    void reset() noexcept {
        for (auto& arm : arms_) {
            arm = BanditArm(config_.initial_alpha, config_.initial_beta);
        }
    }
    
    // Warm-start from historical data
    void warmStart(MLMarketState state, double win_rate, size_t num_samples) noexcept {
        size_t idx = static_cast<size_t>(state);
        if (idx >= NUM_STATES) return;
        
        double wins = win_rate * num_samples;
        double losses = (1.0 - win_rate) * num_samples;
        arms_[idx].alpha += wins;
        arms_[idx].beta += losses;
        arms_[idx].samples += num_samples;
    }
    
    // =========================================================================
    // Stats / Diagnostics
    // =========================================================================
    
    const BanditArm& getArm(MLMarketState state) const noexcept {
        size_t idx = static_cast<size_t>(state) % NUM_STATES;
        return arms_[idx];
    }
    
    void printStats() const noexcept {
        std::printf("[ContextualBandit] State Statistics:\n");
        const char* names[] = {"DEAD", "TRENDING", "RANGING", "VOLATILE"};
        for (size_t i = 0; i < NUM_STATES; ++i) {
            const auto& arm = arms_[i];
            std::printf("  %s: α=%.2f β=%.2f mean=%.3f samples=%lu\n",
                        names[i], arm.alpha, arm.beta, arm.mean(),
                        (unsigned long)arm.samples);
        }
    }
    
    BanditConfig& config() noexcept { return config_; }
    const BanditConfig& config() const noexcept { return config_; }
    
private:
    BanditConfig config_;
    std::array<BanditArm, NUM_STATES> arms_;
    std::mt19937 rng_;
};

// =============================================================================
// Regime-Aware Bandit - Separate bandits per volatility regime
// =============================================================================
class RegimeBandit {
public:
    static constexpr size_t NUM_REGIMES = 4;
    
    RegimeBandit() noexcept = default;
    
    double chooseMultiplier(MLRegime regime, MLMarketState state, double expected_R) noexcept {
        size_t idx = static_cast<size_t>(regime);
        if (idx >= NUM_REGIMES) idx = 1;  // Default to NORMAL_VOL
        return bandits_[idx].chooseMultiplier(state, expected_R);
    }
    
    void update(MLRegime regime, MLMarketState state, double reward) noexcept {
        size_t idx = static_cast<size_t>(regime);
        if (idx >= NUM_REGIMES) idx = 1;
        bandits_[idx].update(state, reward);
    }
    
    ContextualBandit& get(MLRegime regime) noexcept {
        return bandits_[static_cast<size_t>(regime) % NUM_REGIMES];
    }
    
    void reset() noexcept {
        for (auto& b : bandits_) {
            b.reset();
        }
    }
    
private:
    std::array<ContextualBandit, NUM_REGIMES> bandits_;
};

} // namespace ML
} // namespace Chimera
