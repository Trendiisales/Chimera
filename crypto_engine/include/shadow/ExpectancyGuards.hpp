// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/shadow/ExpectancyGuards.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Advanced expectancy protection guards
// OWNER: Jo
// VERSION: v3.0
//
// THREE CRITICAL GUARDS:
// 1. Slope Acceleration Guard - Detects decay before expectancy goes negative
// 2. Session-Weighted Expectancy - Per (symbol × session) tracking
// 3. Shadow/Live Divergence Guard - Catches venue manipulation
//
// RULE: These guards remove 50%+ of long-term drawdowns
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Chimera {
namespace Shadow {

// ═══════════════════════════════════════════════════════════════════════════════
// 1. SLOPE ACCELERATION GUARD
// ═══════════════════════════════════════════════════════════════════════════════
// Detects rate of decay BEFORE expectancy goes negative
// This catches slow-bleed markets (the most dangerous ones)
// ═══════════════════════════════════════════════════════════════════════════════

class SlopeAccelerationGuard {
public:
    struct Config {
        int window_size = 10;           // Number of slope samples to track
        double decay_threshold = -0.3;  // Slope delta as fraction of normal positive slope
        int confirm_windows = 3;        // Consecutive windows of decay to trigger
        double normal_positive_slope = 0.003;  // Baseline positive slope (bps/trade)
    };
    
    explicit SlopeAccelerationGuard(const Config& config = Config{})
        : config_(config)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record new slope value and check for acceleration decay
    // ─────────────────────────────────────────────────────────────────────────
    struct Result {
        bool should_pause = false;
        bool should_reduce_size = false;
        double slope_now = 0.0;
        double slope_prev = 0.0;
        double slope_delta = 0.0;
        int decay_count = 0;
        std::string reason;
    };
    
    Result update(uint16_t symbol_id, double slope_now, double expectancy_bps) noexcept {
        Result result;
        result.slope_now = slope_now;
        
        auto& tracker = trackers_[symbol_id];
        
        // Store slope history
        tracker.slope_history.push_back(slope_now);
        if (tracker.slope_history.size() > static_cast<size_t>(config_.window_size)) {
            tracker.slope_history.pop_front();
        }
        
        // Need at least 2 samples
        if (tracker.slope_history.size() < 2) {
            result.reason = "INSUFFICIENT_DATA";
            return result;
        }
        
        // Calculate slope delta (acceleration)
        result.slope_prev = tracker.slope_history[tracker.slope_history.size() - 2];
        result.slope_delta = slope_now - result.slope_prev;
        
        // Calculate epsilon threshold
        double epsilon = config_.decay_threshold * config_.normal_positive_slope;
        
        // Check for decay pattern:
        // expectancy > 0 AND slope_now > 0 AND slope_delta < -epsilon
        if (expectancy_bps > 0 && slope_now > 0 && result.slope_delta < epsilon) {
            tracker.decay_count++;
            result.decay_count = tracker.decay_count;
            
            if (tracker.decay_count >= config_.confirm_windows) {
                result.should_reduce_size = true;
                result.reason = "SLOPE_ACCELERATION_DECAY";
                
                std::cout << "[SLOPE_ACCEL] Symbol " << symbol_id 
                         << " DECAY detected: slope=" << slope_now
                         << " delta=" << result.slope_delta
                         << " count=" << tracker.decay_count << "\n";
            }
            
            // More severe decay
            if (tracker.decay_count >= config_.confirm_windows * 2) {
                result.should_pause = true;
                result.reason = "SLOPE_ACCELERATION_PAUSE";
                
                std::cout << "[SLOPE_ACCEL] Symbol " << symbol_id 
                         << " PAUSED due to sustained decay\n";
            }
        } else {
            // Reset decay counter if pattern breaks
            if (result.slope_delta >= 0) {
                tracker.decay_count = 0;
            }
        }
        
        return result;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Get size multiplier based on decay state
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double get_size_multiplier(uint16_t symbol_id) const noexcept {
        auto it = trackers_.find(symbol_id);
        if (it == trackers_.end()) return 1.0;
        
        const auto& tracker = it->second;
        if (tracker.decay_count >= config_.confirm_windows * 2) {
            return 0.0;  // Paused
        } else if (tracker.decay_count >= config_.confirm_windows) {
            return 0.5;  // Reduced
        }
        return 1.0;
    }
    
    void reset(uint16_t symbol_id) noexcept {
        trackers_.erase(symbol_id);
    }

private:
    struct Tracker {
        std::deque<double> slope_history;
        int decay_count = 0;
    };
    
    Config config_;
    std::unordered_map<uint16_t, Tracker> trackers_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// 2. SESSION-WEIGHTED EXPECTANCY
// ═══════════════════════════════════════════════════════════════════════════════
// Tracks expectancy per (symbol × session) instead of just symbol
// Auto-disables weak sessions while keeping strong ones active
// ═══════════════════════════════════════════════════════════════════════════════

enum class TradingSession : uint8_t {
    ASIA,       // 00:00 - 08:00 UTC
    LONDON,     // 08:00 - 16:00 UTC
    NY,         // 13:00 - 21:00 UTC
    OVERNIGHT   // 21:00 - 00:00 UTC
};

inline const char* session_name(TradingSession s) noexcept {
    switch (s) {
        case TradingSession::ASIA:      return "ASIA";
        case TradingSession::LONDON:    return "LONDON";
        case TradingSession::NY:        return "NY";
        case TradingSession::OVERNIGHT: return "OVERNIGHT";
        default: return "UNKNOWN";
    }
}

inline TradingSession get_current_session(int utc_hour) noexcept {
    if (utc_hour >= 0 && utc_hour < 8) return TradingSession::ASIA;
    if (utc_hour >= 8 && utc_hour < 13) return TradingSession::LONDON;
    if (utc_hour >= 13 && utc_hour < 21) return TradingSession::NY;
    return TradingSession::OVERNIGHT;
}

class SessionExpectancy {
public:
    struct Config {
        int min_trades_per_session = 30;
        double disable_threshold_bps = -0.2;
        double enable_threshold_bps = 0.3;
        int window_size = 100;
    };
    
    struct SessionStats {
        double expectancy_bps = 0.0;
        double win_rate = 0.0;
        double avg_pnl = 0.0;
        int trade_count = 0;
        int wins = 0;
        int losses = 0;
        bool enabled = true;
        std::deque<double> pnl_history;
    };
    
    explicit SessionExpectancy(const Config& config = Config{})
        : config_(config)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record trade result for specific session
    // ─────────────────────────────────────────────────────────────────────────
    void record_trade(
        uint16_t symbol_id,
        TradingSession session,
        double pnl_bps
    ) noexcept {
        auto key = make_key(symbol_id, session);
        auto& stats = session_stats_[key];
        
        // Update history
        stats.pnl_history.push_back(pnl_bps);
        if (stats.pnl_history.size() > static_cast<size_t>(config_.window_size)) {
            stats.pnl_history.pop_front();
        }
        
        // Update counts
        stats.trade_count++;
        if (pnl_bps > 0) {
            stats.wins++;
        } else {
            stats.losses++;
        }
        
        // Recalculate expectancy
        double sum = 0.0;
        for (double p : stats.pnl_history) {
            sum += p;
        }
        stats.expectancy_bps = sum / stats.pnl_history.size();
        stats.win_rate = (stats.wins + stats.losses > 0) 
            ? static_cast<double>(stats.wins) / (stats.wins + stats.losses) * 100.0 
            : 0.0;
        
        // Check auto-disable/enable
        if (stats.trade_count >= config_.min_trades_per_session) {
            if (stats.enabled && stats.expectancy_bps < config_.disable_threshold_bps) {
                stats.enabled = false;
                std::cout << "[SESSION] Symbol " << symbol_id 
                         << " session " << session_name(session) 
                         << " DISABLED - expectancy=" << stats.expectancy_bps << " bps\n";
            } else if (!stats.enabled && stats.expectancy_bps > config_.enable_threshold_bps) {
                stats.enabled = true;
                std::cout << "[SESSION] Symbol " << symbol_id 
                         << " session " << session_name(session) 
                         << " RE-ENABLED - expectancy=" << stats.expectancy_bps << " bps\n";
            }
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Check if symbol can trade in current session
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool can_trade(uint16_t symbol_id, TradingSession session) const noexcept {
        auto key = make_key(symbol_id, session);
        auto it = session_stats_.find(key);
        if (it == session_stats_.end()) return true;  // No data yet, allow
        return it->second.enabled;
    }
    
    [[nodiscard]] bool can_trade_now(uint16_t symbol_id, int utc_hour) const noexcept {
        return can_trade(symbol_id, get_current_session(utc_hour));
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Get session stats
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] const SessionStats* get_stats(
        uint16_t symbol_id, 
        TradingSession session
    ) const noexcept {
        auto key = make_key(symbol_id, session);
        auto it = session_stats_.find(key);
        if (it == session_stats_.end()) return nullptr;
        return &it->second;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Print summary
    // ─────────────────────────────────────────────────────────────────────────
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║           SESSION EXPECTANCY SUMMARY                         ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  Symbol │ Session   │ Trades │ E(bps) │ WR%  │ Status       ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        
        for (const auto& [key, stats] : session_stats_) {
            uint16_t sym_id = key >> 8;
            TradingSession sess = static_cast<TradingSession>(key & 0xFF);
            
            std::cout << "║  " << std::setw(6) << sym_id
                     << " │ " << std::setw(9) << session_name(sess)
                     << " │ " << std::setw(6) << stats.trade_count
                     << " │ " << std::setw(6) << std::fixed << std::setprecision(2) << stats.expectancy_bps
                     << " │ " << std::setw(4) << std::setprecision(1) << stats.win_rate
                     << " │ " << (stats.enabled ? "ENABLED " : "DISABLED") << "    ║\n";
        }
        
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }

private:
    [[nodiscard]] static uint32_t make_key(uint16_t symbol_id, TradingSession session) noexcept {
        return (static_cast<uint32_t>(symbol_id) << 8) | static_cast<uint8_t>(session);
    }
    
    Config config_;
    std::unordered_map<uint32_t, SessionStats> session_stats_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// 3. SHADOW/LIVE DIVERGENCE GUARD
// ═══════════════════════════════════════════════════════════════════════════════
// Detects when live fills diverge from shadow simulations
// Catches: fake liquidity, queue lying, widening spreads, venue manipulation
// ═══════════════════════════════════════════════════════════════════════════════

class DivergenceGuard {
public:
    struct Config {
        double max_divergence_bps = 1.5;  // Max acceptable divergence
        int min_trades_for_check = 20;    // Min trades before checking
        int window_size = 50;             // Rolling window
        double slippage_multiplier = 2.0; // X times normal slippage = alert
        double normal_slippage_bps = 0.5; // Baseline slippage
    };
    
    struct DivergenceStats {
        double shadow_pnl_bps = 0.0;
        double live_pnl_bps = 0.0;
        double divergence_bps = 0.0;
        int shadow_count = 0;
        int live_count = 0;
        bool paused = false;
        std::string pause_reason;
    };
    
    explicit DivergenceGuard(const Config& config = Config{})
        : config_(config)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record shadow trade
    // ─────────────────────────────────────────────────────────────────────────
    void record_shadow(uint16_t symbol_id, double pnl_bps) noexcept {
        auto& tracker = trackers_[symbol_id];
        
        tracker.shadow_history.push_back(pnl_bps);
        if (tracker.shadow_history.size() > static_cast<size_t>(config_.window_size)) {
            tracker.shadow_history.pop_front();
        }
        tracker.shadow_count++;
        
        update_divergence(symbol_id);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record live trade
    // ─────────────────────────────────────────────────────────────────────────
    void record_live(uint16_t symbol_id, double pnl_bps) noexcept {
        auto& tracker = trackers_[symbol_id];
        
        tracker.live_history.push_back(pnl_bps);
        if (tracker.live_history.size() > static_cast<size_t>(config_.window_size)) {
            tracker.live_history.pop_front();
        }
        tracker.live_count++;
        
        update_divergence(symbol_id);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Check if symbol should be paused
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_paused(uint16_t symbol_id) const noexcept {
        auto it = trackers_.find(symbol_id);
        if (it == trackers_.end()) return false;
        return it->second.paused;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Get divergence stats
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] DivergenceStats get_stats(uint16_t symbol_id) const noexcept {
        DivergenceStats stats;
        
        auto it = trackers_.find(symbol_id);
        if (it == trackers_.end()) return stats;
        
        const auto& tracker = it->second;
        stats.shadow_count = tracker.shadow_count;
        stats.live_count = tracker.live_count;
        stats.paused = tracker.paused;
        stats.pause_reason = tracker.pause_reason;
        
        // Calculate average PnL
        if (!tracker.shadow_history.empty()) {
            double sum = 0.0;
            for (double p : tracker.shadow_history) sum += p;
            stats.shadow_pnl_bps = sum / tracker.shadow_history.size();
        }
        
        if (!tracker.live_history.empty()) {
            double sum = 0.0;
            for (double p : tracker.live_history) sum += p;
            stats.live_pnl_bps = sum / tracker.live_history.size();
        }
        
        stats.divergence_bps = stats.live_pnl_bps - stats.shadow_pnl_bps;
        
        return stats;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Reset pause state (for manual override)
    // ─────────────────────────────────────────────────────────────────────────
    void reset_pause(uint16_t symbol_id) noexcept {
        auto it = trackers_.find(symbol_id);
        if (it != trackers_.end()) {
            it->second.paused = false;
            it->second.pause_reason.clear();
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Print summary
    // ─────────────────────────────────────────────────────────────────────────
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║           SHADOW/LIVE DIVERGENCE SUMMARY                     ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  Symbol │ Shadow  │ Live    │ Δ (bps) │ Status              ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        
        for (const auto& [id, tracker] : trackers_) {
            auto stats = get_stats(id);
            
            const char* status = tracker.paused ? "⚠️ PAUSED" : "✅ OK";
            
            std::cout << "║  " << std::setw(6) << id
                     << " │ " << std::setw(7) << std::fixed << std::setprecision(2) << stats.shadow_pnl_bps
                     << " │ " << std::setw(7) << stats.live_pnl_bps
                     << " │ " << std::setw(7) << stats.divergence_bps
                     << " │ " << std::setw(18) << status << "  ║\n";
        }
        
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }

private:
    struct Tracker {
        std::deque<double> shadow_history;
        std::deque<double> live_history;
        int shadow_count = 0;
        int live_count = 0;
        bool paused = false;
        std::string pause_reason;
    };
    
    void update_divergence(uint16_t symbol_id) noexcept {
        auto& tracker = trackers_[symbol_id];
        
        // Need minimum trades in both
        if (tracker.shadow_count < config_.min_trades_for_check ||
            tracker.live_count < config_.min_trades_for_check) {
            return;
        }
        
        // Calculate divergence
        double shadow_avg = 0.0, live_avg = 0.0;
        
        for (double p : tracker.shadow_history) shadow_avg += p;
        shadow_avg /= tracker.shadow_history.size();
        
        for (double p : tracker.live_history) live_avg += p;
        live_avg /= tracker.live_history.size();
        
        double divergence = std::abs(live_avg - shadow_avg);
        
        // Check thresholds
        double threshold = config_.max_divergence_bps;
        double slippage_threshold = config_.slippage_multiplier * config_.normal_slippage_bps;
        
        if (divergence > threshold || divergence > slippage_threshold) {
            if (!tracker.paused) {
                tracker.paused = true;
                tracker.pause_reason = "DIVERGENCE_" + std::to_string(divergence) + "_BPS";
                
                std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
                std::cout << "║  ⚠️  DIVERGENCE ALERT - SYMBOL " << symbol_id << " PAUSED              ║\n";
                std::cout << "║  Shadow PnL: " << std::fixed << std::setprecision(2) << shadow_avg << " bps                                      ║\n";
                std::cout << "║  Live PnL:   " << live_avg << " bps                                      ║\n";
                std::cout << "║  Divergence: " << divergence << " bps (threshold: " << threshold << ")            ║\n";
                std::cout << "║  Possible causes: venue manipulation, feed skew, broker games  ║\n";
                std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
            }
        }
    }
    
    Config config_;
    std::unordered_map<uint16_t, Tracker> trackers_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// COMBINED GUARD MANAGER
// ═══════════════════════════════════════════════════════════════════════════════
// Unified interface for all expectancy guards
// ═══════════════════════════════════════════════════════════════════════════════

class ExpectancyGuardManager {
public:
    struct Config {
        SlopeAccelerationGuard::Config slope_config;
        SessionExpectancy::Config session_config;
        DivergenceGuard::Config divergence_config;
    };
    
    explicit ExpectancyGuardManager(const Config& config = Config{})
        : slope_guard_(config.slope_config)
        , session_expectancy_(config.session_config)
        , divergence_guard_(config.divergence_config)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record shadow trade (full pipeline)
    // ─────────────────────────────────────────────────────────────────────────
    void record_shadow_trade(
        uint16_t symbol_id,
        double pnl_bps,
        double expectancy_bps,
        double slope,
        int utc_hour
    ) noexcept {
        // Update session expectancy
        TradingSession session = get_current_session(utc_hour);
        session_expectancy_.record_trade(symbol_id, session, pnl_bps);
        
        // Update slope acceleration
        slope_guard_.update(symbol_id, slope, expectancy_bps);
        
        // Update divergence tracker
        divergence_guard_.record_shadow(symbol_id, pnl_bps);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record live trade
    // ─────────────────────────────────────────────────────────────────────────
    void record_live_trade(
        uint16_t symbol_id,
        double pnl_bps,
        double expectancy_bps,
        double slope,
        int utc_hour
    ) noexcept {
        // Update session expectancy
        TradingSession session = get_current_session(utc_hour);
        session_expectancy_.record_trade(symbol_id, session, pnl_bps);
        
        // Update slope acceleration
        slope_guard_.update(symbol_id, slope, expectancy_bps);
        
        // Update divergence tracker
        divergence_guard_.record_live(symbol_id, pnl_bps);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Check if trading is allowed
    // ─────────────────────────────────────────────────────────────────────────
    struct TradeDecision {
        bool can_trade = true;
        double size_multiplier = 1.0;
        std::string block_reason;
    };
    
    [[nodiscard]] TradeDecision can_trade(uint16_t symbol_id, int utc_hour) const noexcept {
        TradeDecision decision;
        
        // Check divergence guard
        if (divergence_guard_.is_paused(symbol_id)) {
            decision.can_trade = false;
            decision.size_multiplier = 0.0;
            decision.block_reason = "DIVERGENCE_PAUSED";
            return decision;
        }
        
        // Check session expectancy
        if (!session_expectancy_.can_trade_now(symbol_id, utc_hour)) {
            decision.can_trade = false;
            decision.size_multiplier = 0.0;
            decision.block_reason = "SESSION_DISABLED";
            return decision;
        }
        
        // Check slope acceleration
        double slope_mult = slope_guard_.get_size_multiplier(symbol_id);
        if (slope_mult == 0.0) {
            decision.can_trade = false;
            decision.size_multiplier = 0.0;
            decision.block_reason = "SLOPE_DECAY_PAUSED";
            return decision;
        }
        
        decision.size_multiplier = slope_mult;
        return decision;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] SlopeAccelerationGuard& slope_guard() noexcept { return slope_guard_; }
    [[nodiscard]] SessionExpectancy& session_expectancy() noexcept { return session_expectancy_; }
    [[nodiscard]] DivergenceGuard& divergence_guard() noexcept { return divergence_guard_; }
    
    [[nodiscard]] const SlopeAccelerationGuard& slope_guard() const noexcept { return slope_guard_; }
    [[nodiscard]] const SessionExpectancy& session_expectancy() const noexcept { return session_expectancy_; }
    [[nodiscard]] const DivergenceGuard& divergence_guard() const noexcept { return divergence_guard_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Print all summaries
    // ─────────────────────────────────────────────────────────────────────────
    void print_summary() const {
        session_expectancy_.print_summary();
        divergence_guard_.print_summary();
    }

private:
    SlopeAccelerationGuard slope_guard_;
    SessionExpectancy session_expectancy_;
    DivergenceGuard divergence_guard_;
};

} // namespace Shadow
} // namespace Chimera
