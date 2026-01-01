// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/RiskAuthority.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Single authority for all size decisions - strategy cannot override
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Control-plane guarantee
//
// PRINCIPLE: "Strategy requests size, Risk Authority decides size"
// - All guards execute in fixed order
// - Non-bypassable
// - Automatically logged
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <iostream>
#include <unordered_map>
#include <ctime>

#include "ExpectancyAuthority.hpp"
#include "ExpectancySlopeGuard.hpp"
#include "BucketQualityGuard.hpp"
#include "SlippageGovernor.hpp"
#include "PortfolioGovernor.hpp"
#include "CapitalRampGovernor.hpp"
#include "../execution/SpreadCaptureGuard.hpp"

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Trade Mode (hard switches)
// ─────────────────────────────────────────────────────────────────────────────
enum class TradeMode : uint8_t {
    OFF,              // No trading at all
    SHADOW,           // Live data, no orders
    PAPER,            // Simulated fills
    LIVE_MAKER_ONLY,  // Real orders, maker only
    LIVE_FULL         // Full live trading
};

inline const char* trade_mode_str(TradeMode m) noexcept {
    switch (m) {
        case TradeMode::OFF:             return "OFF";
        case TradeMode::SHADOW:          return "SHADOW";
        case TradeMode::PAPER:           return "PAPER";
        case TradeMode::LIVE_MAKER_ONLY: return "LIVE_MAKER_ONLY";
        case TradeMode::LIVE_FULL:       return "LIVE_FULL";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal Request (from strategy)
// ─────────────────────────────────────────────────────────────────────────────
struct SignalRequest {
    uint16_t symbol_id = 0;
    std::string symbol_name;
    double requested_size = 0.0;
    double confidence = 0.0;
    bool is_maker = false;
    int utc_hour = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Size Decision (output)
// ─────────────────────────────────────────────────────────────────────────────
struct SizeDecision {
    double final_size = 0.0;
    bool allowed = false;
    
    // Individual multipliers for audit
    double regime_mult = 1.0;
    double expectancy_mult = 1.0;
    double slope_mult = 1.0;
    double bucket_mult = 1.0;
    double slippage_mult = 1.0;
    double capture_mult = 1.0;
    double portfolio_mult = 1.0;
    double capital_mult = 1.0;
    
    const char* block_reason = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Risk Authority - THE single point of size control
// ─────────────────────────────────────────────────────────────────────────────
class RiskAuthority {
public:
    RiskAuthority() 
        : mode_(TradeMode::SHADOW)  // Default: SHADOW (safe)
    {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // CORE: Calculate final size (non-bypassable)
    // Order: Regime → Expectancy → Slope → Bucket → Slippage → Capture → Portfolio → Capital
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] SizeDecision calculate_size(const SignalRequest& req) noexcept {
        SizeDecision decision;
        decision.final_size = req.requested_size;
        
        // ─────────────────────────────────────────────────────────────────
        // 0. Mode check (first)
        // ─────────────────────────────────────────────────────────────────
        if (mode_ == TradeMode::OFF) {
            decision.final_size = 0.0;
            decision.block_reason = "MODE_OFF";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 1. Regime guard (from external classifier)
        // ─────────────────────────────────────────────────────────────────
        if (regime_blocked_[req.symbol_id]) {
            decision.final_size = 0.0;
            decision.regime_mult = 0.0;
            decision.block_reason = "REGIME_TOXIC";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 2. Expectancy authority (dual horizon)
        // ─────────────────────────────────────────────────────────────────
        auto& exp_auth = get_expectancy_authority(req.symbol_id);
        decision.expectancy_mult = exp_auth.size_multiplier();
        decision.final_size *= decision.expectancy_mult;
        
        if (decision.expectancy_mult <= 0.0) {
            decision.block_reason = "EXPECTANCY_DISABLED";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 3. Expectancy slope (non-stationarity)
        // ─────────────────────────────────────────────────────────────────
        auto& slope_guard = get_slope_guard(req.symbol_id);
        decision.slope_mult = slope_guard.size_multiplier();
        decision.final_size *= decision.slope_mult;
        
        if (decision.slope_mult <= 0.0) {
            decision.block_reason = "SLOPE_DECAY";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 4. Time bucket quality
        // ─────────────────────────────────────────────────────────────────
        auto& bucket_mgr = get_bucket_manager(req.symbol_id);
        decision.bucket_mult = bucket_mgr.size_multiplier(req.utc_hour);
        decision.final_size *= decision.bucket_mult;
        
        if (decision.bucket_mult <= 0.0) {
            decision.block_reason = "BUCKET_DISABLED";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 5. Slippage governor
        // ─────────────────────────────────────────────────────────────────
        decision.slippage_mult = slippage_governor_.size_multiplier(req.symbol_id);
        decision.final_size *= decision.slippage_mult;
        
        if (decision.slippage_mult <= 0.0) {
            decision.block_reason = "SLIPPAGE_CRITICAL";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 6. Spread capture (maker orders only)
        // ─────────────────────────────────────────────────────────────────
        if (req.is_maker) {
            decision.capture_mult = spread_capture_.maker_multiplier(req.symbol_id);
            decision.final_size *= decision.capture_mult;
            
            if (!spread_capture_.allow_maker(req.symbol_id)) {
                decision.final_size = 0.0;
                decision.block_reason = "MAKER_DISABLED";
                log_block(req, decision);
                return decision;
            }
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 7. Portfolio governor (correlation + median expectancy)
        // ─────────────────────────────────────────────────────────────────
        decision.portfolio_mult = portfolio_governor_.size_scalar(req.symbol_name);
        decision.portfolio_mult *= portfolio_governor_.portfolio_expectancy_multiplier();
        decision.final_size *= decision.portfolio_mult;
        
        if (decision.portfolio_mult <= 0.0 || portfolio_governor_.portfolio_paused()) {
            decision.block_reason = "PORTFOLIO_PAUSED";
            log_block(req, decision);
            return decision;
        }
        
        if (!portfolio_governor_.can_add_position(req.symbol_name, decision.final_size * 0.01)) {
            decision.final_size = 0.0;
            decision.block_reason = "PORTFOLIO_LIMIT";
            log_block(req, decision);
            return decision;
        }
        
        // ─────────────────────────────────────────────────────────────────
        // 8. Capital ramp governor
        // ─────────────────────────────────────────────────────────────────
        decision.capital_mult = capital_ramp_.size_multiplier();
        decision.final_size *= decision.capital_mult;
        
        // ─────────────────────────────────────────────────────────────────
        // Final decision
        // ─────────────────────────────────────────────────────────────────
        decision.allowed = decision.final_size > 0.0;
        
        if (!decision.allowed) {
            decision.block_reason = "SIZE_ZERO";
            log_block(req, decision);
        }
        
        return decision;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Update methods (called by engine, not strategy)
    // ═══════════════════════════════════════════════════════════════════════
    
    void set_regime_blocked(uint16_t symbol_id, bool blocked) noexcept {
        if (symbol_id < MAX_SYMBOLS) {
            regime_blocked_[symbol_id] = blocked;
        }
    }
    
    void record_trade_pnl(uint16_t symbol_id, double pnl_bps) noexcept {
        auto& exp_auth = get_expectancy_authority(symbol_id);
        exp_auth.record(pnl_bps);
        
        auto& slope = get_slope_guard(symbol_id);
        slope.update(exp_auth.slow_expectancy());
    }
    
    void update_bucket(uint16_t symbol_id, int utc_hour, double session_expectancy) noexcept {
        auto& bucket_mgr = get_bucket_manager(symbol_id);
        bucket_mgr.update(get_bucket(utc_hour), session_expectancy);
    }
    
    void record_slippage(uint16_t symbol_id, double expected, double fill, bool is_buy) noexcept {
        slippage_governor_.record_fill(symbol_id, expected, fill, is_buy);
    }
    
    void record_spread_capture(uint16_t symbol_id, double mid, double fill, 
                               double spread, bool is_buy) noexcept {
        spread_capture_.update_from_fill(symbol_id, mid, fill, spread, is_buy);
    }
    
    void update_portfolio_expectancy(const std::string& symbol, double expectancy) noexcept {
        portfolio_governor_.update_symbol_expectancy(symbol, expectancy);
    }
    
    void end_of_day(double daily_pnl_R, double equity) noexcept {
        capital_ramp_.end_of_day(daily_pnl_R, equity);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Mode control (config only, not runtime)
    // ═══════════════════════════════════════════════════════════════════════
    void set_mode(TradeMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] TradeMode mode() const noexcept { return mode_; }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Getters for monitoring
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] const PortfolioGovernor& portfolio() const noexcept { return portfolio_governor_; }
    [[nodiscard]] const CapitalRampGovernor& capital_ramp() const noexcept { return capital_ramp_; }
    [[nodiscard]] const SlippageGovernor& slippage() const noexcept { return slippage_governor_; }
    
    void print_status() const {
        std::cout << "\n╔═══════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                    RISK AUTHORITY STATUS                          ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ Mode: " << trade_mode_str(mode_) << "\n";
        std::cout << "║ Capital Ramp: " << ramp_level_str(capital_ramp_.level()) 
                  << " (" << capital_ramp_.profitable_days() << " profitable days)\n";
        std::cout << "║ Portfolio Median E: " << portfolio_governor_.median_expectancy() << " bps\n";
        std::cout << "║ Portfolio Mult: " << portfolio_governor_.portfolio_expectancy_multiplier() << "x\n";
        std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n\n";
    }

private:
    static constexpr size_t MAX_SYMBOLS = 32;
    
    void log_block(const SignalRequest& req, const SizeDecision& decision) const {
        std::cout << "[RISK-BLOCK] " << req.symbol_name
                  << " reason=" << (decision.block_reason ? decision.block_reason : "UNKNOWN")
                  << " req=" << req.requested_size
                  << " final=" << decision.final_size
                  << " [E=" << decision.expectancy_mult
                  << " S=" << decision.slope_mult
                  << " B=" << decision.bucket_mult
                  << " P=" << decision.portfolio_mult << "]\n";
    }
    
    ExpectancyAuthority& get_expectancy_authority(uint16_t symbol_id) {
        if (expectancy_authorities_.find(symbol_id) == expectancy_authorities_.end()) {
            expectancy_authorities_[symbol_id] = ExpectancyAuthority();
        }
        return expectancy_authorities_[symbol_id];
    }
    
    ExpectancySlopeGuard& get_slope_guard(uint16_t symbol_id) {
        if (slope_guards_.find(symbol_id) == slope_guards_.end()) {
            slope_guards_[symbol_id] = ExpectancySlopeGuard();
        }
        return slope_guards_[symbol_id];
    }
    
    BucketQualityManager& get_bucket_manager(uint16_t symbol_id) {
        if (bucket_managers_.find(symbol_id) == bucket_managers_.end()) {
            bucket_managers_[symbol_id] = BucketQualityManager();
        }
        return bucket_managers_[symbol_id];
    }
    
    TradeMode mode_;
    
    // Per-symbol guards
    std::unordered_map<uint16_t, ExpectancyAuthority> expectancy_authorities_;
    std::unordered_map<uint16_t, ExpectancySlopeGuard> slope_guards_;
    std::unordered_map<uint16_t, BucketQualityManager> bucket_managers_;
    std::array<bool, MAX_SYMBOLS> regime_blocked_{};
    
    // Shared guards
    SlippageGovernor slippage_governor_;
    Execution::SpreadCaptureManager spread_capture_;
    PortfolioGovernor portfolio_governor_;
    CapitalRampGovernor capital_ramp_;
};

} // namespace Risk
} // namespace Chimera
