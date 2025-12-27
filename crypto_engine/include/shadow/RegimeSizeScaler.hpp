// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/shadow/RegimeSizeScaler.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Regime-aware position sizing - scales size based on proven edge
// OWNER: Jo
// VERSION: v3.0
//
// DESIGN:
// - Size scaling is disabled until MIN_SHADOW_TRADES completed
// - Only 4 regimes: TOXIC, NEUTRAL, EDGE, STRONG_EDGE
// - Never exceed 1.25x base size
// - Global capital governor prevents correlation blowups
//
// RULE: Size cannot increase unless edge is proven
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace Chimera {
namespace Shadow {

// ─────────────────────────────────────────────────────────────────────────────
// Regime States (only 4 - simple, clear)
// ─────────────────────────────────────────────────────────────────────────────
enum class Regime : uint8_t {
    TOXIC,        // Trade blocked
    NEUTRAL,      // Tiny probe only (0.25x)
    EDGE,         // Normal size (1.0x)
    STRONG_EDGE   // Cautiously increased (1.25x MAX)
};

inline const char* regime_str(Regime r) noexcept {
    switch (r) {
        case Regime::TOXIC:       return "TOXIC";
        case Regime::NEUTRAL:     return "NEUTRAL";
        case Regime::EDGE:        return "EDGE";
        case Regime::STRONG_EDGE: return "STRONG_EDGE";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Stats (input to regime classification)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolStats {
    double expectancy_bps = 0.0;
    double expectancy_slope = 0.0;
    double win_rate = 0.0;
    double slippage_bps = 0.0;
    double expected_slippage = 0.3;  // Baseline
    int trade_count = 0;
    bool latency_stressed = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Scaler
// ─────────────────────────────────────────────────────────────────────────────
class RegimeSizeScaler {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    struct Config {
        int min_shadow_trades = 100;           // Must complete before scaling
        double base_size = 0.00008;            // BTC - base position size
        double max_total_risk_pct = 0.5;       // % of equity max total risk
        
        // Regime thresholds
        double toxic_expectancy = -0.5;        // bps
        double toxic_slope = -0.005;           // bps/trade
        double toxic_slippage_mult = 1.5;      // x expected
        
        double neutral_min_expectancy = 0.0;   // bps
        double neutral_max_expectancy = 0.5;   // bps
        
        double edge_min_expectancy = 0.5;      // bps
        double edge_min_slope = 0.002;         // bps/trade
        double edge_min_winrate = 52.0;        // %
        
        double strong_min_expectancy = 1.2;    // bps
        double strong_min_slope = 0.005;       // bps/trade
        double strong_min_winrate = 55.0;      // %
    };
    
    explicit RegimeSizeScaler(const Config& config = Config{})
        : config_(config)
    {
        std::cout << "[REGIME] Size scaler initialized - min " << config_.min_shadow_trades 
                  << " trades before scaling\n";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Classify Regime
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] Regime classify(const SymbolStats& stats) const noexcept {
        // Not enough data - NEUTRAL
        if (stats.trade_count < config_.min_shadow_trades) {
            return Regime::NEUTRAL;
        }
        
        // TOXIC checks
        if (stats.expectancy_bps < config_.toxic_expectancy ||
            stats.expectancy_slope < config_.toxic_slope ||
            stats.slippage_bps > config_.toxic_slippage_mult * stats.expected_slippage) {
            return Regime::TOXIC;
        }
        
        // STRONG_EDGE checks (most restrictive first)
        if (stats.expectancy_bps >= config_.strong_min_expectancy &&
            stats.expectancy_slope >= config_.strong_min_slope &&
            stats.win_rate >= config_.strong_min_winrate &&
            !stats.latency_stressed) {
            return Regime::STRONG_EDGE;
        }
        
        // EDGE checks
        if (stats.expectancy_bps >= config_.edge_min_expectancy &&
            stats.expectancy_slope >= config_.edge_min_slope &&
            stats.win_rate >= config_.edge_min_winrate) {
            return Regime::EDGE;
        }
        
        // NEUTRAL - between toxic and edge
        if (stats.expectancy_bps >= config_.neutral_min_expectancy &&
            stats.expectancy_bps < config_.neutral_max_expectancy &&
            stats.expectancy_slope >= 0) {
            return Regime::NEUTRAL;
        }
        
        // Default to NEUTRAL if unclear
        return Regime::NEUTRAL;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Get Regime Multiplier
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double get_multiplier(Regime regime) const noexcept {
        switch (regime) {
            case Regime::TOXIC:       return 0.0;    // No trading
            case Regime::NEUTRAL:     return 0.25;   // Tiny probe
            case Regime::EDGE:        return 1.0;    // Normal
            case Regime::STRONG_EDGE: return 1.25;   // MAX - never higher
            default: return 0.0;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Calculate Final Size
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double calculate_size(
        uint16_t symbol_id,
        const SymbolStats& stats,
        double latency_factor = 1.0,
        double venue_health_factor = 1.0
    ) noexcept {
        // Classify regime
        Regime regime = classify(stats);
        double regime_mult = get_multiplier(regime);
        
        // Store for monitoring
        symbol_regimes_[symbol_id] = regime;
        
        // If toxic, return 0
        if (regime == Regime::TOXIC) {
            return 0.0;
        }
        
        // Calculate final size
        double final_size = config_.base_size 
                          * regime_mult 
                          * latency_factor 
                          * venue_health_factor;
        
        // Apply global capital governor
        final_size = apply_capital_governor(symbol_id, final_size);
        
        return final_size;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Update Symbol Stats (call after each trade)
    // ─────────────────────────────────────────────────────────────────────────
    void update_stats(
        uint16_t symbol_id,
        double expectancy_bps,
        double slope,
        double win_rate,
        double slippage_bps,
        int trade_count
    ) noexcept {
        SymbolStats& stats = symbol_stats_[symbol_id];
        stats.expectancy_bps = expectancy_bps;
        stats.expectancy_slope = slope;
        stats.win_rate = win_rate;
        stats.slippage_bps = slippage_bps;
        stats.trade_count = trade_count;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Getters
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] Regime get_regime(uint16_t symbol_id) const noexcept {
        auto it = symbol_regimes_.find(symbol_id);
        if (it == symbol_regimes_.end()) return Regime::NEUTRAL;
        return it->second;
    }
    
    [[nodiscard]] const SymbolStats* get_stats(uint16_t symbol_id) const noexcept {
        auto it = symbol_stats_.find(symbol_id);
        if (it == symbol_stats_.end()) return nullptr;
        return &it->second;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Print Status
    // ─────────────────────────────────────────────────────────────────────────
    void print_status() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║              REGIME SIZE SCALER STATUS                       ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        
        for (const auto& [id, regime] : symbol_regimes_) {
            auto stats_it = symbol_stats_.find(id);
            if (stats_it == symbol_stats_.end()) continue;
            
            const auto& stats = stats_it->second;
            double mult = get_multiplier(regime);
            
            std::cout << "║  Symbol " << id << ": " 
                     << std::setw(12) << std::left << regime_str(regime)
                     << " | E=" << std::fixed << std::setprecision(2) << stats.expectancy_bps << " bps"
                     << " | WR=" << std::setprecision(1) << stats.win_rate << "%"
                     << " | " << mult << "x          ║\n";
        }
        
        std::cout << "║  Total Risk Used: " << std::setprecision(2) 
                 << (total_risk_used_ / config_.max_total_risk_pct * 100) << "%"
                 << "                                     ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Global Capital Governor
    // ─────────────────────────────────────────────────────────────────────────
    double apply_capital_governor(uint16_t symbol_id, double proposed_size) noexcept {
        // Calculate current total risk (simplified - assume equal weight per symbol)
        double symbol_risk = proposed_size / config_.base_size * 0.1;  // % risk per trade
        
        // Check if adding this would exceed total
        double new_total = total_risk_used_ + symbol_risk;
        
        if (new_total > config_.max_total_risk_pct) {
            // Scale down proportionally
            double scale = (config_.max_total_risk_pct - total_risk_used_) / symbol_risk;
            scale = std::max(0.0, std::min(1.0, scale));
            proposed_size *= scale;
            
            if (scale < 0.5) {
                std::cout << "[REGIME] Capital governor limiting " << symbol_id 
                         << " - total risk at " << (total_risk_used_ * 100) << "%\n";
            }
        }
        
        // Update tracking
        symbol_risk_[symbol_id] = proposed_size / config_.base_size * 0.1;
        
        // Recalculate total
        total_risk_used_ = 0.0;
        for (const auto& [id, risk] : symbol_risk_) {
            total_risk_used_ += risk;
        }
        
        return proposed_size;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────────
    Config config_;
    
    std::unordered_map<uint16_t, SymbolStats> symbol_stats_;
    std::unordered_map<uint16_t, Regime> symbol_regimes_;
    std::unordered_map<uint16_t, double> symbol_risk_;
    
    double total_risk_used_ = 0.0;
};

} // namespace Shadow
} // namespace Chimera
