// ═══════════════════════════════════════════════════════════════════════════════
// include/system/FeedbackController.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.24: UNIFIED FEEDBACK CONTROLLER (FOUNDATION)
// STATUS: 🔧 ACTIVE
// OWNER: Jo
// CREATED: 2026-01-03
//
// THE SPINE: Ties execution, alpha trust, capital, and risk together.
//
// This is the single authority for:
//   - "Is this alpha still good right now?"
//   - "How much should we trust this trade?"
//   - "What is the adjusted edge after realized costs?"
//
// RESPONSIBILITIES:
//   1. Collect realized execution outcomes
//   2. Update alpha trust dynamically
//   3. Influence execution governors
//   4. Feed confidence-weighted sizing
//   5. Drive early exits + retirement
//
// CREATES A CLOSED ADAPTIVE LOOP WITHOUT:
//   - ML trading
//   - Curve fitting
//   - Overfitting risk
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <atomic>
#include <algorithm>

#include "alpha/MarketRegime.hpp"

namespace Chimera {
namespace System {

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
enum class Venue : uint8_t {
    BINANCE_SPOT = 0,
    BINANCE_FUTURES,
    BLACKBULL_FIX,
    UNKNOWN
};

inline const char* venueStr(Venue v) {
    switch (v) {
        case Venue::BINANCE_SPOT:    return "BINANCE_SPOT";
        case Venue::BINANCE_FUTURES: return "BINANCE_FUTURES";
        case Venue::BLACKBULL_FIX:   return "BLACKBULL_FIX";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cost Delta Sample (per trade)
// ─────────────────────────────────────────────────────────────────────────────
struct CostDeltaSample {
    double expected_cost_bps = 0.0;
    double realized_cost_bps = 0.0;
    double slippage_bps = 0.0;
    double spread_bps = 0.0;
    double latency_ms = 0.0;
    Venue venue = Venue::UNKNOWN;
    char symbol[16] = {};
    uint64_t ts_ns = 0;
    
    [[nodiscard]] double delta() const noexcept {
        return realized_cost_bps - expected_cost_bps;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Trade Intent (submitted)
// ─────────────────────────────────────────────────────────────────────────────
struct TradeIntent {
    uint64_t intent_id = 0;
    const char* alpha_name = nullptr;
    char symbol[16] = {};
    uint16_t symbol_id = 0;
    Venue venue = Venue::UNKNOWN;
    
    enum class Side { BUY, SELL } side = Side::BUY;
    double size = 0.0;
    double expected_entry_price = 0.0;
    double expected_cost_bps = 0.0;
    double predicted_edge_bps = 0.0;
    
    uint64_t submit_ts_ns = 0;
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
};

// ─────────────────────────────────────────────────────────────────────────────
// Fill Report (executed)
// ─────────────────────────────────────────────────────────────────────────────
struct FillReport {
    uint64_t intent_id = 0;
    double fill_price = 0.0;
    double fill_size = 0.0;
    double realized_slippage_bps = 0.0;
    double realized_spread_bps = 0.0;
    double latency_ms = 0.0;
    uint64_t fill_ts_ns = 0;
    bool filled = false;
    bool rejected = false;
    const char* reject_reason = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Trade Outcome (closed)
// ─────────────────────────────────────────────────────────────────────────────
struct TradeOutcome {
    uint64_t intent_id = 0;
    const char* alpha_name = nullptr;
    char symbol[16] = {};
    
    double entry_price = 0.0;
    double exit_price = 0.0;
    double pnl_bps = 0.0;
    double mfe_bps = 0.0;
    double mae_bps = 0.0;
    
    double realized_cost_bps = 0.0;  // Total cost (spread + slip + fees)
    double expected_cost_bps = 0.0;
    
    uint64_t hold_time_ms = 0;
    
    enum class ExitReason {
        TP_HIT,
        SL_HIT,
        TIME_STOP,
        EARLY_EXIT,      // Shape-aware early exit
        MANUAL
    } exit_reason = ExitReason::TIME_STOP;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol-Venue Cost Tracker
// ─────────────────────────────────────────────────────────────────────────────
struct CostTracker {
    char symbol[16] = {};
    Venue venue = Venue::UNKNOWN;
    
    // Rolling EMA of cost error
    double cost_error_bps = 0.0;        // EMA(realized - expected)
    double slippage_error_bps = 0.0;    // EMA(realized_slip - expected_slip)
    
    // Counts
    uint64_t samples = 0;
    uint64_t consecutive_worse = 0;     // Consecutive samples where realized > expected
    
    // Session buckets (24 hours)
    std::array<double, 24> hourly_cost_error = {};
    std::array<uint32_t, 24> hourly_samples = {};
    
    static constexpr double EMA_ALPHA = 0.1;
    
    void update(const CostDeltaSample& s) {
        double delta = s.delta();
        
        // EMA update
        if (samples == 0) {
            cost_error_bps = delta;
            slippage_error_bps = s.slippage_bps;
        } else {
            cost_error_bps = EMA_ALPHA * delta + (1.0 - EMA_ALPHA) * cost_error_bps;
            slippage_error_bps = EMA_ALPHA * s.slippage_bps + (1.0 - EMA_ALPHA) * slippage_error_bps;
        }
        
        ++samples;
        
        // Track consecutive worse
        if (delta > 0) {
            ++consecutive_worse;
        } else {
            consecutive_worse = 0;
        }
        
        // Hourly bucket
        uint64_t hour = (s.ts_ns / 3'600'000'000'000ULL) % 24;
        hourly_cost_error[hour] = EMA_ALPHA * delta + (1.0 - EMA_ALPHA) * hourly_cost_error[hour];
        ++hourly_samples[hour];
    }
    
    [[nodiscard]] bool shouldDegrade() const noexcept {
        return cost_error_bps > 0.4;
    }
    
    [[nodiscard]] bool shouldSuspend() const noexcept {
        return cost_error_bps > 0.7 || consecutive_worse >= 5;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Alpha Trust State
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaTrust {
    char alpha_name[32] = {};
    char symbol[16] = {};
    
    // Trade stats
    uint64_t n_trades = 0;
    uint64_t n_wins = 0;
    uint64_t n_losses = 0;
    
    // Expectancy
    double total_pnl_bps = 0.0;
    double expectancy_bps = 0.0;
    double expectancy_prev = 0.0;       // For slope calculation
    double expectancy_slope = 0.0;      // Trend of expectancy
    
    // Win/Loss stats
    double avg_win_bps = 0.0;
    double avg_loss_bps = 0.0;
    double win_rate = 0.0;
    
    // Drawdown
    double peak_pnl = 0.0;
    double current_dd = 0.0;
    double max_dd = 0.0;
    
    // Cost attribution
    double avg_cost_error = 0.0;
    
    // Failure shapes (counts)
    uint32_t immediate_rejections = 0;  // mfe < 0.3 bps
    uint32_t fake_impulses = 0;         // mfe > 0.6 then reversal
    uint32_t chop_bleeds = 0;           // many small losses
    uint32_t drift_decays = 0;          // slow drift loss
    
    // Recent shape counts (rolling 15 trades)
    std::array<uint8_t, 15> recent_shapes = {};  // 0=win, 1=imm_rej, 2=fake_imp, 3=chop, 4=drift
    uint8_t recent_idx = 0;
    
    // Session stats
    uint64_t session_trades = 0;
    uint64_t session_losses = 0;
    double session_pnl = 0.0;
    
    void update(const TradeOutcome& o, uint8_t shape = 0) {
        ++n_trades;
        total_pnl_bps += o.pnl_bps;
        
        if (o.pnl_bps > 0) {
            ++n_wins;
            avg_win_bps = (avg_win_bps * (n_wins - 1) + o.pnl_bps) / n_wins;
        } else {
            ++n_losses;
            avg_loss_bps = (avg_loss_bps * (n_losses - 1) + o.pnl_bps) / n_losses;
        }
        
        win_rate = (n_trades > 0) ? static_cast<double>(n_wins) / n_trades : 0.0;
        
        // Expectancy and slope
        expectancy_prev = expectancy_bps;
        expectancy_bps = (n_trades > 0) ? total_pnl_bps / n_trades : 0.0;
        if (n_trades > 5) {
            expectancy_slope = 0.2 * (expectancy_bps - expectancy_prev) + 0.8 * expectancy_slope;
        }
        
        // Drawdown
        if (total_pnl_bps > peak_pnl) peak_pnl = total_pnl_bps;
        current_dd = peak_pnl - total_pnl_bps;
        if (current_dd > max_dd) max_dd = current_dd;
        
        // Cost error
        double cost_delta = o.realized_cost_bps - o.expected_cost_bps;
        avg_cost_error = 0.1 * cost_delta + 0.9 * avg_cost_error;
        
        // Shape tracking
        recent_shapes[recent_idx % 15] = shape;
        ++recent_idx;
        
        if (shape == 1) ++immediate_rejections;
        else if (shape == 2) ++fake_impulses;
        else if (shape == 3) ++chop_bleeds;
        else if (shape == 4) ++drift_decays;
        
        // Session
        ++session_trades;
        if (o.pnl_bps < 0) ++session_losses;
        session_pnl += o.pnl_bps;
    }
    
    void reset_session() {
        session_trades = 0;
        session_losses = 0;
        session_pnl = 0.0;
    }
    
    [[nodiscard]] uint32_t recent_fake_impulses() const noexcept {
        uint32_t count = 0;
        for (size_t i = 0; i < 15 && i < n_trades; ++i) {
            if (recent_shapes[i] == 2) ++count;
        }
        return count;
    }
    
    [[nodiscard]] uint32_t recent_chop_losses() const noexcept {
        uint32_t count = 0;
        for (size_t i = 0; i < 15 && i < n_trades; ++i) {
            if (recent_shapes[i] == 3) ++count;
        }
        return count;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Confidence Score Calculator
// ─────────────────────────────────────────────────────────────────────────────
struct ConfidenceScore {
    double score = 0.0;                 // 0.0 to 1.0
    
    enum class Tier {
        SHADOW_ONLY,    // < 0.25
        PROBE,          // 0.25 - 0.5
        PARTIAL_LIVE,   // 0.5 - 0.8
        FULL_LIVE       // > 0.8
    } tier = Tier::SHADOW_ONLY;
    
    static constexpr double N_FULL = 50.0;
    
    static double sigmoid(double x) noexcept {
        return 1.0 / (1.0 + std::exp(-x));
    }
    
    static ConfidenceScore calculate(const AlphaTrust& trust) {
        ConfidenceScore cs;
        
        // confidence = sqrt(n/50) * sigmoid(exp/0.5) * sigmoid(slope/0.1)
        double n_factor = std::sqrt(std::min(static_cast<double>(trust.n_trades) / N_FULL, 1.0));
        double exp_factor = sigmoid(trust.expectancy_bps / 0.5);
        double slope_factor = sigmoid(trust.expectancy_slope / 0.1);
        
        cs.score = std::clamp(n_factor * exp_factor * slope_factor, 0.0, 1.0);
        
        // Penalties
        if (trust.expectancy_bps < 0) cs.score *= 0.3;
        if (trust.expectancy_slope < -0.05) cs.score *= 0.5;
        if (trust.avg_cost_error > 0.5) cs.score *= 0.7;
        if (trust.recent_fake_impulses() >= 3) cs.score *= 0.4;
        
        // Determine tier
        if (cs.score < 0.25) {
            cs.tier = Tier::SHADOW_ONLY;
        } else if (cs.score < 0.5) {
            cs.tier = Tier::PROBE;
        } else if (cs.score < 0.8) {
            cs.tier = Tier::PARTIAL_LIVE;
        } else {
            cs.tier = Tier::FULL_LIVE;
        }
        
        return cs;
    }
};

inline const char* tierStr(ConfidenceScore::Tier t) {
    switch (t) {
        case ConfidenceScore::Tier::SHADOW_ONLY:  return "SHADOW";
        case ConfidenceScore::Tier::PROBE:        return "PROBE";
        case ConfidenceScore::Tier::PARTIAL_LIVE: return "PARTIAL";
        case ConfidenceScore::Tier::FULL_LIVE:    return "FULL";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FEEDBACK CONTROLLER (THE SPINE)
// ─────────────────────────────────────────────────────────────────────────────
class FeedbackController {
public:
    static constexpr size_t MAX_COST_TRACKERS = 64;
    static constexpr size_t MAX_ALPHA_TRUST = 32;
    
    static FeedbackController& instance() {
        static FeedbackController fc;
        return fc;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // TRADE LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    void onTradeSubmitted(const TradeIntent& intent) {
        // Record intent for later matching
        for (size_t i = 0; i < MAX_PENDING; ++i) {
            if (pending_[i].intent_id == 0) {
                pending_[i] = intent;
                break;
            }
        }
        
        printf("[FEEDBACK] Intent submitted: %s %s id=%llu\n",
               intent.alpha_name, intent.symbol,
               static_cast<unsigned long long>(intent.intent_id));
    }
    
    void onTradeFilled(const FillReport& fill) {
        // Find matching intent
        TradeIntent* intent = nullptr;
        for (size_t i = 0; i < MAX_PENDING; ++i) {
            if (pending_[i].intent_id == fill.intent_id) {
                intent = &pending_[i];
                break;
            }
        }
        
        if (!intent) {
            printf("[FEEDBACK] WARNING: No intent found for fill id=%llu\n",
                   static_cast<unsigned long long>(fill.intent_id));
            return;
        }
        
        if (fill.rejected) {
            // Track rejection
            printf("[FEEDBACK] Rejection: %s %s reason=%s\n",
                   intent->alpha_name, intent->symbol,
                   fill.reject_reason ? fill.reject_reason : "unknown");
            intent->intent_id = 0;  // Clear slot
            return;
        }
        
        // Record cost sample
        CostDeltaSample sample;
        sample.expected_cost_bps = intent->expected_cost_bps;
        sample.realized_cost_bps = fill.realized_slippage_bps + fill.realized_spread_bps + 5.0; // +5 bps fee estimate
        sample.slippage_bps = fill.realized_slippage_bps;
        sample.spread_bps = fill.realized_spread_bps;
        sample.latency_ms = fill.latency_ms;
        sample.venue = intent->venue;
        strncpy(sample.symbol, intent->symbol, 15);
        sample.ts_ns = fill.fill_ts_ns;
        
        // Update cost tracker
        CostTracker* ct = getOrCreateCostTracker(intent->symbol, intent->venue);
        if (ct) {
            ct->update(sample);
            
            // Check for degradation
            if (ct->shouldSuspend()) {
                printf("[FEEDBACK] ⚠️  VENUE SUSPEND: %s on %s cost_error=%.2f bps\n",
                       venueStr(intent->venue), intent->symbol, ct->cost_error_bps);
            } else if (ct->shouldDegrade()) {
                printf("[FEEDBACK] ⚠️  VENUE DEGRADE: %s on %s cost_error=%.2f bps\n",
                       venueStr(intent->venue), intent->symbol, ct->cost_error_bps);
            }
        }
        
        printf("[FEEDBACK] Fill: %s %s slip=%.2f spread=%.2f latency=%.1fms\n",
               intent->alpha_name, intent->symbol,
               fill.realized_slippage_bps, fill.realized_spread_bps, fill.latency_ms);
    }
    
    void onTradeClosed(const TradeOutcome& outcome) {
        // Classify loss shape
        uint8_t shape = classifyLossShape(outcome);
        
        // Update alpha trust
        AlphaTrust* trust = getOrCreateAlphaTrust(outcome.alpha_name, outcome.symbol);
        if (trust) {
            trust->update(outcome, shape);
            
            // Calculate confidence
            ConfidenceScore conf = ConfidenceScore::calculate(*trust);
            
            printf("[FEEDBACK] Closed: %s %s pnl=%.2f exp=%.2f conf=%.2f [%s]\n",
                   outcome.alpha_name, outcome.symbol,
                   outcome.pnl_bps, trust->expectancy_bps,
                   conf.score, tierStr(conf.tier));
            
            // Check kill conditions
            checkKillConditions(*trust);
        }
        
        // Clear pending intent
        for (size_t i = 0; i < MAX_PENDING; ++i) {
            if (pending_[i].intent_id == outcome.intent_id) {
                pending_[i].intent_id = 0;
                break;
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERIES
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] double adjustedEdge(const char* alpha, const char* symbol) const {
        const AlphaTrust* trust = getAlphaTrust(alpha, symbol);
        const CostTracker* cost = getCostTracker(symbol, Venue::BINANCE_SPOT);
        
        if (!trust) return 0.0;
        
        double raw_edge = trust->expectancy_bps;
        double cost_penalty = cost ? cost->cost_error_bps : 0.0;
        
        return raw_edge - cost_penalty;
    }
    
    [[nodiscard]] ConfidenceScore confidenceScore(const char* alpha, const char* symbol) const {
        const AlphaTrust* trust = getAlphaTrust(alpha, symbol);
        if (!trust) return ConfidenceScore{};
        return ConfidenceScore::calculate(*trust);
    }
    
    [[nodiscard]] bool shouldThrottle(const char* alpha, const char* symbol) const {
        const AlphaTrust* trust = getAlphaTrust(alpha, symbol);
        if (!trust) return true;
        
        // Throttle conditions
        if (trust->recent_fake_impulses() >= 3) return true;
        if (trust->session_losses >= 3) return true;
        if (trust->expectancy_slope < -0.1) return true;
        
        return false;
    }
    
    [[nodiscard]] double positionSizeMultiplier(const char* alpha, const char* symbol) const {
        ConfidenceScore conf = confidenceScore(alpha, symbol);
        
        switch (conf.tier) {
            case ConfidenceScore::Tier::SHADOW_ONLY:  return 0.0;
            case ConfidenceScore::Tier::PROBE:        return 0.1;
            case ConfidenceScore::Tier::PARTIAL_LIVE: return 0.5;
            case ConfidenceScore::Tier::FULL_LIVE:    return 1.0;
            default: return 0.0;
        }
    }
    
    [[nodiscard]] bool shouldSuspendVenue(const char* symbol, Venue venue) const {
        const CostTracker* ct = getCostTracker(symbol, venue);
        return ct && ct->shouldSuspend();
    }
    
    [[nodiscard]] bool shouldDegradeVenue(const char* symbol, Venue venue) const {
        const CostTracker* ct = getCostTracker(symbol, venue);
        return ct && ct->shouldDegrade();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ACCESSORS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const AlphaTrust* getAlphaTrust(const char* alpha, const char* symbol) const {
        for (size_t i = 0; i < trust_count_; ++i) {
            if (strcmp(trusts_[i].alpha_name, alpha) == 0 &&
                strcmp(trusts_[i].symbol, symbol) == 0) {
                return &trusts_[i];
            }
        }
        return nullptr;
    }
    
    [[nodiscard]] const CostTracker* getCostTracker(const char* symbol, Venue venue) const {
        for (size_t i = 0; i < cost_count_; ++i) {
            if (strcmp(costs_[i].symbol, symbol) == 0 && costs_[i].venue == venue) {
                return &costs_[i];
            }
        }
        return nullptr;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REPORTING
    // ═══════════════════════════════════════════════════════════════════════
    
    void printStatus() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ FEEDBACK CONTROLLER STATUS                                           ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        for (size_t i = 0; i < trust_count_; ++i) {
            const AlphaTrust& t = trusts_[i];
            ConfidenceScore conf = ConfidenceScore::calculate(t);
            
            printf("║ %-16s %-8s n=%3llu exp=%+.2f wr=%.0f%% conf=%.2f [%s]\n",
                   t.alpha_name, t.symbol,
                   static_cast<unsigned long long>(t.n_trades),
                   t.expectancy_bps,
                   t.win_rate * 100.0,
                   conf.score,
                   tierStr(conf.tier));
            printf("║   └─ shapes: rej=%u fake=%u chop=%u drift=%u\n",
                   t.immediate_rejections, t.fake_impulses,
                   t.chop_bleeds, t.drift_decays);
        }
        
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ COST TRACKERS                                                        ║\n");
        
        for (size_t i = 0; i < cost_count_; ++i) {
            const CostTracker& c = costs_[i];
            printf("║ %-8s %-12s err=%+.2f slip=%+.2f samples=%llu %s\n",
                   c.symbol, venueStr(c.venue),
                   c.cost_error_bps, c.slippage_error_bps,
                   static_cast<unsigned long long>(c.samples),
                   c.shouldSuspend() ? "⛔ SUSPEND" : (c.shouldDegrade() ? "⚠️  DEGRADE" : "✓"));
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    FeedbackController() = default;
    
    // ═══════════════════════════════════════════════════════════════════════
    // INTERNAL
    // ═══════════════════════════════════════════════════════════════════════
    
    AlphaTrust* getOrCreateAlphaTrust(const char* alpha, const char* symbol) {
        for (size_t i = 0; i < trust_count_; ++i) {
            if (strcmp(trusts_[i].alpha_name, alpha) == 0 &&
                strcmp(trusts_[i].symbol, symbol) == 0) {
                return &trusts_[i];
            }
        }
        
        if (trust_count_ >= MAX_ALPHA_TRUST) return nullptr;
        
        AlphaTrust& t = trusts_[trust_count_++];
        strncpy(t.alpha_name, alpha, 31);
        strncpy(t.symbol, symbol, 15);
        return &t;
    }
    
    CostTracker* getOrCreateCostTracker(const char* symbol, Venue venue) {
        for (size_t i = 0; i < cost_count_; ++i) {
            if (strcmp(costs_[i].symbol, symbol) == 0 && costs_[i].venue == venue) {
                return &costs_[i];
            }
        }
        
        if (cost_count_ >= MAX_COST_TRACKERS) return nullptr;
        
        CostTracker& c = costs_[cost_count_++];
        strncpy(c.symbol, symbol, 15);
        c.venue = venue;
        return &c;
    }
    
    [[nodiscard]] uint8_t classifyLossShape(const TradeOutcome& o) const {
        if (o.pnl_bps >= 0) return 0;  // Win
        
        // Immediate rejection: MFE < 0.3 bps
        if (o.mfe_bps < 0.3) return 1;
        
        // Fake impulse: MFE > 0.6 then reversal to loss
        if (o.mfe_bps > 0.6 && o.pnl_bps < -0.5) return 2;
        
        // Drift decay: slow loss (long hold, small MAE)
        if (o.hold_time_ms > 250 && std::abs(o.mae_bps) < 1.0) return 4;
        
        // Chop bleed: everything else (small loss)
        if (o.pnl_bps > -1.0) return 3;
        
        return 0;
    }
    
    void checkKillConditions(const AlphaTrust& trust) {
        // 3-tier early kill system
        
        // TIER 1: Early Kill (first 10-12 trades)
        if (trust.n_trades <= 12) {
            // Kill if expectancy < -0.5 after 10 trades
            if (trust.n_trades >= 10 && trust.expectancy_bps < -0.5) {
                printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ ⛔ TIER 1 EARLY KILL: %s on %s                                   \n",
                       trust.alpha_name, trust.symbol);
                printf("║   Expectancy %.2f bps after %llu trades (< -0.5 threshold)           \n",
                       trust.expectancy_bps, static_cast<unsigned long long>(trust.n_trades));
                printf("║   ACTION: IMMEDIATE RETIREMENT                                       ║\n");
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
            }
            
            // Kill if win rate < 30% after 12 trades
            if (trust.n_trades >= 12 && trust.win_rate < 0.30) {
                printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ ⛔ TIER 1 EARLY KILL: %s on %s                                   \n",
                       trust.alpha_name, trust.symbol);
                printf("║   Win rate %.0f%% after %llu trades (< 30%% threshold)                \n",
                       trust.win_rate * 100.0, static_cast<unsigned long long>(trust.n_trades));
                printf("║   ACTION: IMMEDIATE RETIREMENT                                       ║\n");
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
            }
        }
        
        // Shape-based kills (any time)
        if (trust.recent_fake_impulses() >= 3) {
            printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
            printf("║ ⛔ SHAPE KILL: %s on %s                                          \n",
                   trust.alpha_name, trust.symbol);
            printf("║   3+ fake impulse failures in last 15 trades                         ║\n");
            printf("║   ACTION: SUSPEND ALPHA → RESET TO SHADOW                            ║\n");
            printf("╚══════════════════════════════════════════════════════════════════════╝\n");
        }
        
        if (trust.session_losses >= 5) {
            printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
            printf("║ ⚠️  SESSION THROTTLE: %s on %s                                     \n",
                   trust.alpha_name, trust.symbol);
            printf("║   5+ losses in session → FREQUENCY CAP                               ║\n");
            printf("╚══════════════════════════════════════════════════════════════════════╝\n");
        }
    }
    
    static constexpr size_t MAX_PENDING = 32;
    std::array<TradeIntent, MAX_PENDING> pending_{};
    std::array<AlphaTrust, MAX_ALPHA_TRUST> trusts_{};
    std::array<CostTracker, MAX_COST_TRACKERS> costs_{};
    size_t trust_count_ = 0;
    size_t cost_count_ = 0;
};

} // namespace System
} // namespace Chimera
