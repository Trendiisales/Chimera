// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ExecutionCostModel.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.23
// PURPOSE: Execution cost modeling for alpha filtering
// OWNER: Jo
// CREATED: 2026-01-03
//
// This is LAYER C of the adaptive stack:
//   A "good signal" that can't be executed profitably is NOT alpha.
//
// DESIGN:
//   - Per-symbol execution cost tracking
//   - Spread, fee, slippage components
//   - Net edge calculation
//   - Alpha should check net_edge before trading
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <algorithm>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Cost Components (in basis points)
// ─────────────────────────────────────────────────────────────────────────────
struct ExecutionCost {
    double spread_bps{0.0};      // Current spread cost (half-spread for execution)
    double fee_bps{0.0};         // Exchange fee (maker vs taker)
    double slippage_bps{0.0};    // Observed slippage (average)
    double latency_cost_bps{0.0};// Estimated cost from latency (price movement during execution)
    
    // Total execution cost
    [[nodiscard]] double total_bps() const noexcept {
        return spread_bps + fee_bps + slippage_bps + latency_cost_bps;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Net Edge Calculation
// ─────────────────────────────────────────────────────────────────────────────

// Calculate net edge after execution costs
// Returns: edge in bps that would actually be captured
[[nodiscard]] inline double net_edge_bps(double raw_edge_bps, const ExecutionCost& cost) noexcept {
    return raw_edge_bps - cost.total_bps();
}

// Check if a trade is worth executing
[[nodiscard]] inline bool edge_survives_execution(double raw_edge_bps, 
                                                   const ExecutionCost& cost,
                                                   double min_net_edge_bps = 0.5) noexcept {
    double net = net_edge_bps(raw_edge_bps, cost);
    return net >= min_net_edge_bps;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution Cost Model (Per-Symbol)
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionCostModel {
public:
    static constexpr size_t MAX_SYMBOLS = 64;
    
    static ExecutionCostModel& instance() {
        static ExecutionCostModel m;
        return m;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UPDATE COSTS (called by OrderSender / Market Data)
    // ═══════════════════════════════════════════════════════════════════════
    
    void update_spread(const char* symbol, double spread_bps) {
        ExecutionCost* c = get_or_create(symbol);
        if (c) {
            // Use half-spread as cost (we pay half on entry, half on exit conceptually)
            c->spread_bps = spread_bps / 2.0;
        }
    }
    
    void update_fee(const char* symbol, double fee_bps, bool is_maker) {
        ExecutionCost* c = get_or_create(symbol);
        if (c) {
            c->fee_bps = fee_bps;
            // Store maker/taker info for reference
            is_maker_[get_index(symbol)] = is_maker;
        }
    }
    
    void record_slippage(const char* symbol, double slippage_bps) {
        ExecutionCost* c = get_or_create(symbol);
        if (c) {
            // Exponential moving average of slippage
            double alpha = 0.1;  // 10% weight to new observation
            c->slippage_bps = (1.0 - alpha) * c->slippage_bps + alpha * slippage_bps;
        }
    }
    
    void estimate_latency_cost(const char* symbol, uint64_t latency_us, double volatility_bps_per_sec) {
        ExecutionCost* c = get_or_create(symbol);
        if (c) {
            // Estimate price movement during execution latency
            double latency_sec = static_cast<double>(latency_us) / 1'000'000.0;
            c->latency_cost_bps = volatility_bps_per_sec * latency_sec;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY COSTS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] ExecutionCost get_cost(const char* symbol) const {
        size_t idx = get_index(symbol);
        if (idx < symbol_count_.load()) {
            return costs_[idx];
        }
        // Return default (conservative) if symbol not found
        return ExecutionCost{2.0, 5.0, 1.0, 0.5};  // ~8.5bps total
    }
    
    // Quick check: should we trade?
    [[nodiscard]] bool should_trade(const char* symbol, 
                                     double raw_edge_bps,
                                     double min_net_edge_bps = 0.5) const {
        ExecutionCost cost = get_cost(symbol);
        return edge_survives_execution(raw_edge_bps, cost, min_net_edge_bps);
    }
    
    // Get detailed breakdown
    struct CostBreakdown {
        const char* symbol;
        double spread_bps;
        double fee_bps;
        double slippage_bps;
        double latency_bps;
        double total_bps;
        bool is_maker;
    };
    
    [[nodiscard]] CostBreakdown breakdown(const char* symbol) const {
        size_t idx = get_index(symbol);
        CostBreakdown b{};
        b.symbol = symbol;
        
        if (idx < symbol_count_.load()) {
            const ExecutionCost& c = costs_[idx];
            b.spread_bps = c.spread_bps;
            b.fee_bps = c.fee_bps;
            b.slippage_bps = c.slippage_bps;
            b.latency_bps = c.latency_cost_bps;
            b.total_bps = c.total_bps();
            b.is_maker = is_maker_[idx];
        }
        
        return b;
    }
    
    // Print all costs
    void print_all() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ EXECUTION COST MODEL                                                 ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Symbol     Spread    Fee    Slip   Lat    Total   Type              ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        for (size_t i = 0; i < symbol_count_.load(); ++i) {
            const ExecutionCost& c = costs_[i];
            printf("║ %-10s %5.2f    %5.2f  %5.2f  %5.2f  %6.2f  %s              ║\n",
                   symbols_[i],
                   c.spread_bps, c.fee_bps, c.slippage_bps, c.latency_cost_bps,
                   c.total_bps(),
                   is_maker_[i] ? "MAKER" : "TAKER");
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    ExecutionCostModel() : symbol_count_(0) {
        for (auto& s : symbols_) s[0] = '\0';
        for (auto& m : is_maker_) m = false;
    }
    
    size_t get_index(const char* symbol) const {
        for (size_t i = 0; i < symbol_count_.load(); ++i) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return i;
            }
        }
        return MAX_SYMBOLS;  // Not found
    }
    
    ExecutionCost* get_or_create(const char* symbol) {
        size_t idx = get_index(symbol);
        if (idx < symbol_count_.load()) {
            return &costs_[idx];
        }
        
        // Create new
        idx = symbol_count_.fetch_add(1);
        if (idx >= MAX_SYMBOLS) {
            symbol_count_.fetch_sub(1);
            return nullptr;
        }
        
        strncpy(symbols_[idx], symbol, 15);
        symbols_[idx][15] = '\0';
        return &costs_[idx];
    }
    
    char symbols_[MAX_SYMBOLS][16];
    ExecutionCost costs_[MAX_SYMBOLS];
    bool is_maker_[MAX_SYMBOLS];
    std::atomic<size_t> symbol_count_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Filter Result (for GUI explanation)
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaFilterResult {
    bool passed{false};
    double raw_edge_bps{0.0};
    double net_edge_bps{0.0};
    double min_required_bps{0.0};
    const char* reject_reason{nullptr};
    
    static AlphaFilterResult pass(double raw, double net) {
        return {true, raw, net, 0.0, nullptr};
    }
    
    static AlphaFilterResult reject(double raw, double net, double min_req, const char* reason) {
        return {false, raw, net, min_req, reason};
    }
};

// Evaluate an alpha signal against execution costs
[[nodiscard]] inline AlphaFilterResult evaluate_alpha(
    const char* symbol,
    double raw_edge_bps,
    double min_net_edge_bps = 0.5
) {
    ExecutionCost cost = ExecutionCostModel::instance().get_cost(symbol);
    double net = net_edge_bps(raw_edge_bps, cost);
    
    if (net >= min_net_edge_bps) {
        return AlphaFilterResult::pass(raw_edge_bps, net);
    } else {
        return AlphaFilterResult::reject(raw_edge_bps, net, min_net_edge_bps,
                                         "EDGE_KILLED_BY_EXECUTION");
    }
}

} // namespace Chimera
