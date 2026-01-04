// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/ShadowTrader.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.23: SHADOW TRADING (MANDATORY PHASE 1 TESTING)
// STATUS: 🔧 ACTIVE
// OWNER: Jo
// CREATED: 2026-01-03
//
// PURPOSE: Validate alpha expectancy WITHOUT sending real orders
//
// PHASE 1 — SHADOW MODE:
//   - Live market data (real WS feeds)
//   - NO ORDERS sent
//   - Alpha fires virtually
//   - Real spread, real latency
//   - Log everything
//
// IF EXPECTANCY ≤ 0 → DELETE ALPHA (no negotiation)
//
// WHAT WE LOG:
//   - timestamp
//   - symbol
//   - regime
//   - ofi
//   - spread
//   - latency
//   - virtual_entry_price
//   - virtual_exit_price
//   - exit_reason
//   - pnl_bps
//
// CONFIDENCE GATE:
//   Enable live only when:
//   - trades ≥ 50
//   - expectancy ≥ +0.6 bps
//   - max DD < 3R
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <atomic>
#include <fstream>

#include "alpha/MarketRegime.hpp"
#include "alpha/AlphaRetirement.hpp"

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Trade Record
// ─────────────────────────────────────────────────────────────────────────────
struct ShadowTrade {
    // Identification
    uint64_t trade_id = 0;
    const char* alpha_name = nullptr;
    const char* symbol = nullptr;
    uint16_t symbol_id = 0;
    
    // Context at entry
    MarketRegime regime = MarketRegime::DEAD;
    double ofi = 0.0;
    double spread_bps = 0.0;
    double latency_us = 0.0;
    
    // Entry
    uint64_t entry_ts_ns = 0;
    double entry_price = 0.0;
    double entry_mid = 0.0;
    enum class Side { BUY, SELL } side = Side::BUY;
    
    // Exit
    uint64_t exit_ts_ns = 0;
    double exit_price = 0.0;
    double exit_mid = 0.0;
    enum class ExitReason {
        NONE,
        TP_HIT,
        SL_HIT,
        TIME_STOP,
        COUNTER_FLOW,
        MANUAL
    } exit_reason = ExitReason::NONE;
    
    // PnL calculation
    double tp_bps = 0.0;
    double sl_bps = 0.0;
    double pnl_bps = 0.0;
    double mae_bps = 0.0;                   // Max adverse excursion
    double mfe_bps = 0.0;                   // Max favorable excursion
    
    // State
    bool is_open = false;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Calculate PnL from current price
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double calc_pnl_bps(double current_mid) const noexcept {
        if (entry_mid <= 0) return 0.0;
        double pnl = (side == Side::BUY) 
            ? (current_mid - entry_mid) / entry_mid * 10000.0
            : (entry_mid - current_mid) / entry_mid * 10000.0;
        return pnl;
    }
    
    void update_excursions(double current_mid) noexcept {
        double pnl = calc_pnl_bps(current_mid);
        if (pnl < mae_bps) mae_bps = pnl;
        if (pnl > mfe_bps) mfe_bps = pnl;
    }
    
    [[nodiscard]] bool check_exit(double current_mid, uint64_t now_ns, uint64_t max_hold_ns) noexcept {
        update_excursions(current_mid);
        double pnl = calc_pnl_bps(current_mid);
        
        // TP hit
        if (pnl >= tp_bps) {
            exit_reason = ExitReason::TP_HIT;
            exit_price = current_mid;
            exit_mid = current_mid;
            exit_ts_ns = now_ns;
            pnl_bps = pnl;
            is_open = false;
            return true;
        }
        
        // SL hit
        if (pnl <= -sl_bps) {
            exit_reason = ExitReason::SL_HIT;
            exit_price = current_mid;
            exit_mid = current_mid;
            exit_ts_ns = now_ns;
            pnl_bps = pnl;
            is_open = false;
            return true;
        }
        
        // Time stop
        if (now_ns - entry_ts_ns >= max_hold_ns) {
            exit_reason = ExitReason::TIME_STOP;
            exit_price = current_mid;
            exit_mid = current_mid;
            exit_ts_ns = now_ns;
            pnl_bps = pnl;
            is_open = false;
            return true;
        }
        
        return false;
    }
};

inline const char* exitReasonStr(ShadowTrade::ExitReason r) {
    switch (r) {
        case ShadowTrade::ExitReason::NONE:         return "NONE";
        case ShadowTrade::ExitReason::TP_HIT:       return "TP_HIT";
        case ShadowTrade::ExitReason::SL_HIT:       return "SL_HIT";
        case ShadowTrade::ExitReason::TIME_STOP:    return "TIME_STOP";
        case ShadowTrade::ExitReason::COUNTER_FLOW: return "COUNTER_FLOW";
        case ShadowTrade::ExitReason::MANUAL:       return "MANUAL";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Trading Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct ShadowStats {
    char alpha_name[32] = {};
    char symbol[16] = {};
    
    uint64_t trades = 0;
    uint64_t wins = 0;
    uint64_t losses = 0;
    
    double total_pnl_bps = 0.0;
    double expectancy_bps = 0.0;
    double win_rate = 0.0;
    
    double max_drawdown_bps = 0.0;
    double peak_pnl = 0.0;
    double current_dd = 0.0;
    
    double avg_win_bps = 0.0;
    double avg_loss_bps = 0.0;
    double avg_mae_bps = 0.0;
    double avg_mfe_bps = 0.0;
    
    // Confidence gate
    bool gate_passed = false;
    
    void update(const ShadowTrade& t) {
        ++trades;
        total_pnl_bps += t.pnl_bps;
        
        if (t.pnl_bps > 0) {
            ++wins;
            avg_win_bps = (avg_win_bps * (wins - 1) + t.pnl_bps) / wins;
        } else {
            ++losses;
            avg_loss_bps = (avg_loss_bps * (losses - 1) + t.pnl_bps) / losses;
        }
        
        win_rate = (trades > 0) ? static_cast<double>(wins) / trades : 0.0;
        expectancy_bps = (trades > 0) ? total_pnl_bps / trades : 0.0;
        
        // MAE/MFE tracking
        avg_mae_bps = (avg_mae_bps * (trades - 1) + t.mae_bps) / trades;
        avg_mfe_bps = (avg_mfe_bps * (trades - 1) + t.mfe_bps) / trades;
        
        // Drawdown
        if (total_pnl_bps > peak_pnl) {
            peak_pnl = total_pnl_bps;
        }
        current_dd = peak_pnl - total_pnl_bps;
        if (current_dd > max_drawdown_bps) {
            max_drawdown_bps = current_dd;
        }
        
        // Confidence gate check
        check_gate();
    }
    
    void check_gate() {
        // trades ≥ 50 AND expectancy ≥ +0.6 bps AND max DD < 3R
        if (trades < 50) {
            gate_passed = false;
            return;
        }
        
        if (expectancy_bps < 0.6) {
            gate_passed = false;
            return;
        }
        
        // 3R drawdown check
        double avg_loss = (losses > 0) ? std::abs(avg_loss_bps) : 1.0;
        double r_multiple = max_drawdown_bps / avg_loss;
        if (r_multiple >= 3.0) {
            gate_passed = false;
            return;
        }
        
        gate_passed = true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Trader (manages shadow positions)
// ─────────────────────────────────────────────════════════════════════════════
class ShadowTrader {
public:
    static constexpr size_t MAX_OPEN = 16;
    static constexpr size_t MAX_STATS = 32;
    
    static ShadowTrader& instance() {
        static ShadowTrader st;
        return st;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // OPEN SHADOW POSITION
    // ═══════════════════════════════════════════════════════════════════════
    
    bool open_position(
        const char* alpha_name,
        const char* symbol,
        uint16_t symbol_id,
        MarketRegime regime,
        ShadowTrade::Side side,
        double entry_mid,
        double spread_bps,
        double ofi,
        double latency_us,
        double tp_bps,
        double sl_bps,
        uint64_t now_ns
    ) {
        // Find free slot
        ShadowTrade* slot = nullptr;
        for (size_t i = 0; i < MAX_OPEN; ++i) {
            if (!open_trades_[i].is_open) {
                slot = &open_trades_[i];
                break;
            }
        }
        
        if (!slot) {
            printf("[SHADOW] ERROR: No free slots for shadow trade\n");
            return false;
        }
        
        // Initialize
        slot->trade_id = ++trade_counter_;
        slot->alpha_name = alpha_name;
        slot->symbol = symbol;
        slot->symbol_id = symbol_id;
        slot->regime = regime;
        slot->side = side;
        slot->entry_mid = entry_mid;
        slot->entry_price = entry_mid;
        slot->entry_ts_ns = now_ns;
        slot->spread_bps = spread_bps;
        slot->ofi = ofi;
        slot->latency_us = latency_us;
        slot->tp_bps = tp_bps;
        slot->sl_bps = sl_bps;
        slot->mae_bps = 0.0;
        slot->mfe_bps = 0.0;
        slot->is_open = true;
        slot->exit_reason = ShadowTrade::ExitReason::NONE;
        
        printf("[SHADOW] OPEN: %s %s %s @ %.2f (tp=%.1f sl=%.1f)\n",
               alpha_name, symbol,
               side == ShadowTrade::Side::BUY ? "BUY" : "SELL",
               entry_mid, tp_bps, sl_bps);
        
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UPDATE ALL OPEN POSITIONS (call on each tick)
    // ═══════════════════════════════════════════════════════════════════════
    
    void update(uint16_t symbol_id, double current_mid, uint64_t now_ns, uint64_t max_hold_ns) {
        for (size_t i = 0; i < MAX_OPEN; ++i) {
            ShadowTrade& t = open_trades_[i];
            if (!t.is_open || t.symbol_id != symbol_id) continue;
            
            if (t.check_exit(current_mid, now_ns, max_hold_ns)) {
                // Trade closed - record it
                on_close(t);
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool has_position(const char* alpha, const char* symbol) const {
        for (size_t i = 0; i < MAX_OPEN; ++i) {
            if (open_trades_[i].is_open &&
                strcmp(open_trades_[i].alpha_name, alpha) == 0 &&
                strcmp(open_trades_[i].symbol, symbol) == 0) {
                return true;
            }
        }
        return false;
    }
    
    [[nodiscard]] const ShadowStats* get_stats(const char* alpha, const char* symbol) const {
        for (size_t i = 0; i < stats_count_; ++i) {
            if (strcmp(stats_[i].alpha_name, alpha) == 0 &&
                strcmp(stats_[i].symbol, symbol) == 0) {
                return &stats_[i];
            }
        }
        return nullptr;
    }
    
    [[nodiscard]] bool gate_passed(const char* alpha, const char* symbol) const {
        const ShadowStats* s = get_stats(alpha, symbol);
        return s && s->gate_passed;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REPORTING
    // ═══════════════════════════════════════════════════════════════════════
    
    void print_stats() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ SHADOW TRADING STATISTICS                                            ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        for (size_t i = 0; i < stats_count_; ++i) {
            const ShadowStats& s = stats_[i];
            printf("║ %-16s %-8s trades=%3llu exp=%.2f wr=%.1f%% %s\n",
                   s.alpha_name, s.symbol,
                   static_cast<unsigned long long>(s.trades),
                   s.expectancy_bps,
                   s.win_rate * 100.0,
                   s.gate_passed ? "✓ GATE PASSED" : "");
            printf("║   └─ W:%.1f L:%.1f MAE:%.1f MFE:%.1f DD:%.1f\n",
                   s.avg_win_bps, s.avg_loss_bps,
                   s.avg_mae_bps, s.avg_mfe_bps,
                   s.max_drawdown_bps);
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    void write_csv(const char* filepath) const {
        std::ofstream f(filepath);
        if (!f.is_open()) {
            printf("[SHADOW] ERROR: Cannot open %s for writing\n", filepath);
            return;
        }
        
        // Header
        f << "alpha,symbol,trades,wins,losses,total_pnl,expectancy,win_rate,";
        f << "avg_win,avg_loss,avg_mae,avg_mfe,max_dd,gate_passed\n";
        
        for (size_t i = 0; i < stats_count_; ++i) {
            const ShadowStats& s = stats_[i];
            f << s.alpha_name << "," << s.symbol << ",";
            f << s.trades << "," << s.wins << "," << s.losses << ",";
            f << s.total_pnl_bps << "," << s.expectancy_bps << "," << s.win_rate << ",";
            f << s.avg_win_bps << "," << s.avg_loss_bps << ",";
            f << s.avg_mae_bps << "," << s.avg_mfe_bps << ",";
            f << s.max_drawdown_bps << "," << (s.gate_passed ? "1" : "0") << "\n";
        }
        
        f.close();
        printf("[SHADOW] Stats written to %s\n", filepath);
    }

private:
    ShadowTrader() {}
    
    void on_close(const ShadowTrade& t) {
        printf("[SHADOW] CLOSE: %s %s pnl=%.2f (%s) mae=%.2f mfe=%.2f\n",
               t.alpha_name, t.symbol,
               t.pnl_bps, exitReasonStr(t.exit_reason),
               t.mae_bps, t.mfe_bps);
        
        // Update stats
        ShadowStats* stats = get_or_create_stats(t.alpha_name, t.symbol);
        if (stats) {
            stats->update(t);
            
            // Check for auto-delete
            if (stats->trades >= 30 && stats->expectancy_bps < 0) {
                printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ [SHADOW] ✗ ALPHA FAILED: %s on %s                               \n",
                       t.alpha_name, t.symbol);
                printf("║   Expectancy: %.2f bps (NEGATIVE after %llu trades)                  \n",
                       stats->expectancy_bps, static_cast<unsigned long long>(stats->trades));
                printf("║   ACTION: DELETE ALPHA (no negotiation)                              ║\n");
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
            }
            
            // Check gate
            if (stats->gate_passed && !stats_gate_logged_[stats - stats_]) {
                printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
                printf("║ [SHADOW] ✓ GATE PASSED: %s on %s                                \n",
                       t.alpha_name, t.symbol);
                printf("║   Trades: %llu  Expectancy: %.2f bps  Win Rate: %.1f%%              \n",
                       static_cast<unsigned long long>(stats->trades),
                       stats->expectancy_bps,
                       stats->win_rate * 100.0);
                printf("║   READY FOR MICRO-LIVE TESTING                                       ║\n");
                printf("╚══════════════════════════════════════════════════════════════════════╝\n");
                stats_gate_logged_[stats - stats_] = true;
            }
        }
    }
    
    ShadowStats* get_or_create_stats(const char* alpha, const char* symbol) {
        // Find existing
        for (size_t i = 0; i < stats_count_; ++i) {
            if (strcmp(stats_[i].alpha_name, alpha) == 0 &&
                strcmp(stats_[i].symbol, symbol) == 0) {
                return &stats_[i];
            }
        }
        
        // Create new
        if (stats_count_ >= MAX_STATS) return nullptr;
        
        ShadowStats& s = stats_[stats_count_++];
        strncpy(s.alpha_name, alpha, 31);
        strncpy(s.symbol, symbol, 15);
        return &s;
    }
    
    std::array<ShadowTrade, MAX_OPEN> open_trades_{};
    std::array<ShadowStats, MAX_STATS> stats_{};
    std::array<bool, MAX_STATS> stats_gate_logged_{};
    size_t stats_count_ = 0;
    std::atomic<uint64_t> trade_counter_{0};
};

} // namespace Alpha
} // namespace Chimera
