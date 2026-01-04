// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/FailureShapeDetector.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.27
// PURPOSE: Detect failure shapes (LATE_ENTRY, FEE_DOMINATED, FALSE_VACUUM)
// OWNER: Jo
// CREATED: 2026-01-04
//
// v4.9.27 PHASE 4: FAILURE SHAPE DETECTION
// ═══════════════════════════════════════════════════════════════════════════════
//
// THE PROBLEM:
//   Liquidity Vacuum (and other strategies) fail in DISTINCT SHAPES, not random.
//   
//   Without shape detection, you can't tell:
//   - Was this a signal problem?
//   - Was this an execution problem?
//   - Was this a market microstructure problem?
//
// THE SHAPES:
//   1. FALSE_VACUUM
//      - Volume spike (signal fires)
//      - No continuation
//      - Fast reversion
//      → Indicates signal noise, not alpha
//
//   2. LATE_ENTRY
//      - Edge decays before fill
//      - Positive gross edge at signal
//      - Negative net PnL
//      → Indicates latency is eating edge
//
//   3. FEE_DOMINATED
//      - Gross edge > 0
//      - Net < 0 due to taker cost
//      → Indicates edge too thin for fee structure
//
//   4. LIQUIDITY_MIRAGE
//      - Book shows depth
//      - Fill pulls levels
//      - Adverse selection
//      → Indicates toxic flow detection failure
//
// HOW IT CONNECTS:
//   Trade → FailureVector → Shape Classification
//   Shape → AlphaRetirement (retire earlier)
//   Shape → Position Sizing (reduce exposure)
//   Shape → Gate Calibration (tighten/loosen)
//
// USAGE:
//   On every trade exit:
//     detector.record(trade);
//   
//   Periodically:
//     detector.printReport();
//     auto shapes = detector.getShapeSummary();
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <array>
#include <fstream>
#include <atomic>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Failure Shape Enum
// ─────────────────────────────────────────────────────────────────────────────
enum class FailureShape : uint8_t {
    UNKNOWN = 0,         // Couldn't classify
    
    // Signal failures
    FALSE_VACUUM,        // Signal noise, fast reversion
    REGIME_MISMATCH,     // Wrong market regime
    
    // Execution failures
    LATE_ENTRY,          // Latency ate the edge
    SLIPPAGE_DEATH,      // Filled at adverse price
    
    // Cost failures
    FEE_DOMINATED,       // Edge < fees
    SPREAD_EATEN,        // Entry spread killed PnL
    
    // Market structure failures
    LIQUIDITY_MIRAGE,    // Phantom depth
    ADVERSE_SELECTION,   // Toxic flow
    
    // Success (for completeness)
    CLEAN_WIN,           // No failure, clean profit
    
    COUNT
};

inline const char* failureShapeStr(FailureShape s) {
    switch (s) {
        case FailureShape::UNKNOWN:          return "UNKNOWN";
        case FailureShape::FALSE_VACUUM:     return "FALSE_VACUUM";
        case FailureShape::REGIME_MISMATCH:  return "REGIME_MISMATCH";
        case FailureShape::LATE_ENTRY:       return "LATE_ENTRY";
        case FailureShape::SLIPPAGE_DEATH:   return "SLIPPAGE_DEATH";
        case FailureShape::FEE_DOMINATED:    return "FEE_DOMINATED";
        case FailureShape::SPREAD_EATEN:     return "SPREAD_EATEN";
        case FailureShape::LIQUIDITY_MIRAGE: return "LIQUIDITY_MIRAGE";
        case FailureShape::ADVERSE_SELECTION:return "ADVERSE_SELECTION";
        case FailureShape::CLEAN_WIN:        return "CLEAN_WIN";
        default: return "INVALID";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Failure Vector (input data for classification)
// ─────────────────────────────────────────────────────────────────────────────
struct FailureVector {
    // Trade identification
    uint64_t trade_id = 0;
    char alpha_name[32] = {};
    char symbol[16] = {};
    uint64_t timestamp_ns = 0;
    
    // Edge metrics
    double gross_edge_bps = 0.0;        // Edge at signal time
    double net_edge_bps = 0.0;          // Net PnL after all costs
    double edge_decay_bps = 0.0;        // How much edge decayed (gross - realized)
    
    // Timing metrics
    double time_to_fill_ms = 0.0;       // Signal → fill time
    double hold_time_ms = 0.0;          // Entry → exit time
    double post_fill_return_1s = 0.0;   // Return 1s after fill
    
    // Cost metrics
    double fee_bps = 0.0;               // Total fees paid
    double spread_cost_bps = 0.0;       // Spread at entry
    double slippage_bps = 0.0;          // Execution slippage
    
    // Market context
    double pre_signal_vol_1s = 0.0;     // Volume 1s before signal
    double post_signal_vol_1s = 0.0;    // Volume 1s after signal
    double bid_depth_ratio = 0.0;       // Bid depth / Ask depth
    double vpin = 0.0;                  // Toxicity measure
    
    // Outcome
    bool was_winner = false;
    double mfe_bps = 0.0;               // Max favorable excursion
    double mae_bps = 0.0;               // Max adverse excursion
    
    // Classification result
    FailureShape shape = FailureShape::UNKNOWN;
};

// ─────────────────────────────────────────────────────────────────────────────
// Shape Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct ShapeStats {
    FailureShape shape = FailureShape::UNKNOWN;
    
    uint64_t count = 0;
    double total_pnl_bps = 0.0;
    double avg_pnl_bps = 0.0;
    double avg_gross_edge = 0.0;
    double avg_time_to_fill = 0.0;
    double avg_fee_bps = 0.0;
    
    void update(const FailureVector& fv) {
        ++count;
        total_pnl_bps += fv.net_edge_bps;
        avg_pnl_bps = total_pnl_bps / count;
        avg_gross_edge = (avg_gross_edge * (count - 1) + fv.gross_edge_bps) / count;
        avg_time_to_fill = (avg_time_to_fill * (count - 1) + fv.time_to_fill_ms) / count;
        avg_fee_bps = (avg_fee_bps * (count - 1) + fv.fee_bps) / count;
    }
    
    [[nodiscard]] double pnl_impact() const noexcept {
        return total_pnl_bps;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Failure Shape Detector (Singleton)
// ─────────────────────────────────────────────────────────────────────────────
class FailureShapeDetector {
public:
    static FailureShapeDetector& instance() {
        static FailureShapeDetector d;
        return d;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CLASSIFICATION (Core logic)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] FailureShape classify(const FailureVector& fv) const noexcept {
        // Winners don't need failure classification
        if (fv.was_winner && fv.net_edge_bps > 0.5) {
            return FailureShape::CLEAN_WIN;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // LATE_ENTRY: Positive gross edge, but latency killed it
        // Evidence: time_to_fill > threshold AND edge decayed significantly
        // ───────────────────────────────────────────────────────────────────
        if (fv.gross_edge_bps > 0.5 && 
            fv.net_edge_bps < 0.0 && 
            fv.time_to_fill_ms > late_entry_threshold_ms_) {
            return FailureShape::LATE_ENTRY;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // FEE_DOMINATED: Gross edge positive but fees ate it
        // Evidence: gross > 0, fees > gross
        // ───────────────────────────────────────────────────────────────────
        if (fv.gross_edge_bps > 0.0 && 
            fv.fee_bps >= fv.gross_edge_bps * 0.8) {
            return FailureShape::FEE_DOMINATED;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // SPREAD_EATEN: Entry spread killed PnL
        // Evidence: spread cost > realized edge
        // ───────────────────────────────────────────────────────────────────
        if (fv.spread_cost_bps > fv.gross_edge_bps * 0.5 && 
            fv.net_edge_bps < 0.0) {
            return FailureShape::SPREAD_EATEN;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // FALSE_VACUUM: Signal noise, fast reversion
        // Evidence: 1s post-fill return strongly negative
        // ───────────────────────────────────────────────────────────────────
        if (fv.post_fill_return_1s < -1.0 && 
            fv.gross_edge_bps > 0.0) {
            return FailureShape::FALSE_VACUUM;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // ADVERSE_SELECTION: Toxic flow
        // Evidence: High VPIN + negative outcome
        // ───────────────────────────────────────────────────────────────────
        if (fv.vpin > adverse_selection_vpin_threshold_ && 
            fv.net_edge_bps < 0.0) {
            return FailureShape::ADVERSE_SELECTION;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // LIQUIDITY_MIRAGE: Book showed depth but disappeared
        // Evidence: Slippage significantly exceeds expected
        // ───────────────────────────────────────────────────────────────────
        if (fv.slippage_bps > liquidity_mirage_slippage_threshold_ && 
            fv.net_edge_bps < 0.0) {
            return FailureShape::LIQUIDITY_MIRAGE;
        }
        
        // ───────────────────────────────────────────────────────────────────
        // SLIPPAGE_DEATH: Pure execution failure
        // Evidence: High MAE relative to expected
        // ───────────────────────────────────────────────────────────────────
        if (std::abs(fv.mae_bps) > fv.gross_edge_bps * 2.0 && 
            fv.net_edge_bps < 0.0) {
            return FailureShape::SLIPPAGE_DEATH;
        }
        
        // Can't classify
        return FailureShape::UNKNOWN;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RECORDING
    // ═══════════════════════════════════════════════════════════════════════
    
    void record(FailureVector& fv) {
        // Classify the failure
        fv.shape = classify(fv);
        
        // Update stats
        shape_stats_[static_cast<size_t>(fv.shape)].update(fv);
        shape_stats_[static_cast<size_t>(fv.shape)].shape = fv.shape;
        
        // Store in circular buffer
        size_t idx = (write_idx_++) % MAX_HISTORY;
        history_[idx] = fv;
        
        total_trades_++;
        
        // Log interesting shapes
        if (fv.shape != FailureShape::CLEAN_WIN && fv.shape != FailureShape::UNKNOWN) {
            printf("[SHAPE] %s %s → %s (gross=%.2f net=%.2f fill=%.1fms)\n",
                   fv.alpha_name, fv.symbol, 
                   failureShapeStr(fv.shape),
                   fv.gross_edge_bps, fv.net_edge_bps, fv.time_to_fill_ms);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERIES
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const ShapeStats& getStats(FailureShape shape) const {
        return shape_stats_[static_cast<size_t>(shape)];
    }
    
    [[nodiscard]] uint64_t totalTrades() const noexcept {
        return total_trades_;
    }
    
    // Get most damaging failure shape (by total PnL impact)
    [[nodiscard]] FailureShape worstShape() const noexcept {
        FailureShape worst = FailureShape::UNKNOWN;
        double worst_pnl = 0.0;
        
        for (size_t i = 1; i < static_cast<size_t>(FailureShape::COUNT); ++i) {
            FailureShape s = static_cast<FailureShape>(i);
            if (s == FailureShape::CLEAN_WIN) continue;
            
            const auto& stats = shape_stats_[i];
            if (stats.total_pnl_bps < worst_pnl) {
                worst_pnl = stats.total_pnl_bps;
                worst = s;
            }
        }
        return worst;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // REPORTING
    // ═══════════════════════════════════════════════════════════════════════
    
    void printReport() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ FAILURE SHAPE ANALYSIS (v4.9.27)                                     ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Total Trades: %llu                                                    \n",
               static_cast<unsigned long long>(total_trades_));
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        double total_loss = 0.0;
        
        for (size_t i = 0; i < static_cast<size_t>(FailureShape::COUNT); ++i) {
            const auto& s = shape_stats_[i];
            if (s.count == 0) continue;
            
            FailureShape shape = static_cast<FailureShape>(i);
            const char* indicator = "";
            
            // Mark problematic shapes
            if (shape != FailureShape::CLEAN_WIN && 
                shape != FailureShape::UNKNOWN && 
                s.count >= 5 && s.avg_pnl_bps < -0.5) {
                indicator = " ← ACTION NEEDED";
                total_loss += s.total_pnl_bps;
            }
            
            printf("║ %-18s n=%3llu avg=%.2f sum=%.1f fill=%.0fms%s\n",
                   failureShapeStr(shape),
                   static_cast<unsigned long long>(s.count),
                   s.avg_pnl_bps,
                   s.total_pnl_bps,
                   s.avg_time_to_fill,
                   indicator);
        }
        
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Total Loss from Failures: %.1f bps                                   \n", total_loss);
        printf("║ Worst Shape: %s                                              \n",
               failureShapeStr(worstShape()));
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    void printRecommendations() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ FAILURE SHAPE RECOMMENDATIONS                                        ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        bool any = false;
        
        // LATE_ENTRY recommendation
        const auto& late = shape_stats_[static_cast<size_t>(FailureShape::LATE_ENTRY)];
        if (late.count >= 5 && late.avg_pnl_bps < -0.5) {
            any = true;
            printf("║ LATE_ENTRY (%llu trades, %.1f bps lost):\n",
                   (unsigned long long)late.count, late.total_pnl_bps);
            printf("║   → Reduce time_to_fill threshold from %.0fms to %.0fms\n",
                   late_entry_threshold_ms_, late.avg_time_to_fill * 0.8);
            printf("║   → Or increase edge requirement\n");
            printf("║\n");
        }
        
        // FEE_DOMINATED recommendation
        const auto& fee = shape_stats_[static_cast<size_t>(FailureShape::FEE_DOMINATED)];
        if (fee.count >= 5 && fee.avg_pnl_bps < -0.5) {
            any = true;
            printf("║ FEE_DOMINATED (%llu trades, %.1f bps lost):\n",
                   (unsigned long long)fee.count, fee.total_pnl_bps);
            printf("║   → Avg fee: %.2f bps, avg gross edge: %.2f bps\n",
                   fee.avg_fee_bps, fee.avg_gross_edge);
            printf("║   → Increase min_edge from current to %.2f bps\n",
                   fee.avg_fee_bps * 1.5);
            printf("║\n");
        }
        
        // FALSE_VACUUM recommendation
        const auto& fv = shape_stats_[static_cast<size_t>(FailureShape::FALSE_VACUUM)];
        if (fv.count >= 5 && fv.avg_pnl_bps < -0.5) {
            any = true;
            printf("║ FALSE_VACUUM (%llu trades, %.1f bps lost):\n",
                   (unsigned long long)fv.count, fv.total_pnl_bps);
            printf("║   → Add continuation filter (require 2nd tick confirmation)\n");
            printf("║   → Or increase volume spike threshold\n");
            printf("║\n");
        }
        
        // ADVERSE_SELECTION recommendation
        const auto& adv = shape_stats_[static_cast<size_t>(FailureShape::ADVERSE_SELECTION)];
        if (adv.count >= 5 && adv.avg_pnl_bps < -0.5) {
            any = true;
            printf("║ ADVERSE_SELECTION (%llu trades, %.1f bps lost):\n",
                   (unsigned long long)adv.count, adv.total_pnl_bps);
            printf("║   → Tighten VPIN threshold from %.2f to %.2f\n",
                   adverse_selection_vpin_threshold_, 
                   adverse_selection_vpin_threshold_ * 0.9);
            printf("║\n");
        }
        
        if (!any) {
            printf("║ No significant failure shapes detected. Continue monitoring.       ║\n");
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    void writeCSV(const char* filepath) const {
        std::ofstream f(filepath);
        if (!f.is_open()) {
            printf("[SHAPE] ERROR: Cannot open %s\n", filepath);
            return;
        }
        
        f << "shape,count,avg_pnl,total_pnl,avg_gross,avg_fill_ms,avg_fee\n";
        
        for (size_t i = 0; i < static_cast<size_t>(FailureShape::COUNT); ++i) {
            const auto& s = shape_stats_[i];
            if (s.count == 0) continue;
            
            f << failureShapeStr(static_cast<FailureShape>(i)) << ","
              << s.count << ","
              << s.avg_pnl_bps << ","
              << s.total_pnl_bps << ","
              << s.avg_gross_edge << ","
              << s.avg_time_to_fill << ","
              << s.avg_fee_bps << "\n";
        }
        
        f.close();
        printf("[SHAPE] Report written to %s\n", filepath);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void setThresholds(double late_entry_ms, double vpin, double slippage_bps) {
        late_entry_threshold_ms_ = late_entry_ms;
        adverse_selection_vpin_threshold_ = vpin;
        liquidity_mirage_slippage_threshold_ = slippage_bps;
        
        printf("[SHAPE] Thresholds set: late_entry=%.0fms vpin=%.2f slippage=%.1fbps\n",
               late_entry_ms, vpin, slippage_bps);
    }
    
    void reset() {
        for (auto& s : shape_stats_) {
            s = ShapeStats{};
        }
        write_idx_ = 0;
        total_trades_ = 0;
        printf("[SHAPE] Statistics reset\n");
    }

private:
    FailureShapeDetector() = default;
    
    // History buffer
    static constexpr size_t MAX_HISTORY = 1000;
    std::array<FailureVector, MAX_HISTORY> history_{};
    size_t write_idx_ = 0;
    
    // Per-shape statistics
    static constexpr size_t SHAPE_COUNT = static_cast<size_t>(FailureShape::COUNT);
    std::array<ShapeStats, SHAPE_COUNT> shape_stats_{};
    
    // Counters
    uint64_t total_trades_ = 0;
    
    // Classification thresholds (defaults)
    double late_entry_threshold_ms_ = 300.0;           // 300ms = too slow
    double adverse_selection_vpin_threshold_ = 0.65;   // High toxicity
    double liquidity_mirage_slippage_threshold_ = 2.0; // 2 bps slippage = phantom book
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience function to build FailureVector from trade data
// ─────────────────────────────────────────────────────────────────────────────
inline FailureVector buildFailureVector(
    const char* alpha,
    const char* symbol,
    double gross_edge_bps,
    double net_edge_bps,
    double time_to_fill_ms,
    double post_fill_return_1s,
    double fee_bps,
    double spread_bps,
    double vpin,
    double mfe_bps,
    double mae_bps
) {
    FailureVector fv;
    strncpy(fv.alpha_name, alpha, sizeof(fv.alpha_name) - 1);
    strncpy(fv.symbol, symbol, sizeof(fv.symbol) - 1);
    fv.gross_edge_bps = gross_edge_bps;
    fv.net_edge_bps = net_edge_bps;
    fv.time_to_fill_ms = time_to_fill_ms;
    fv.post_fill_return_1s = post_fill_return_1s;
    fv.fee_bps = fee_bps;
    fv.spread_cost_bps = spread_bps;
    fv.vpin = vpin;
    fv.mfe_bps = mfe_bps;
    fv.mae_bps = mae_bps;
    fv.was_winner = net_edge_bps > 0;
    return fv;
}

} // namespace Alpha
} // namespace Chimera
