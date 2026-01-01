#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// ExecutionSlicer - Breaks single entries into micro-slices
// v4.2.2: Reduces adverse selection on thin books (SOL, alts)
// ═══════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cstdint>

namespace Chimera {

struct SlicePlan {
    int slices = 1;
    double qty_per_slice = 0.0;
    uint64_t spacing_ns = 500'000;  // 0.5ms default
};

class ExecutionSlicer {
public:
    // Plan execution slices based on liquidity and spread
    SlicePlan plan(double total_qty,
                   double book_liquidity,
                   double spread_bps) const
    {
        // More slices for:
        // - Wide spreads (thin books)
        // - Low liquidity
        // - Large orders relative to book
        
        int slices = 1;
        
        if (spread_bps > 2.0) {
            slices = 5;  // Very thin book
        } else if (spread_bps > 1.5) {
            slices = 4;  // Thin book
        } else if (book_liquidity < 1000) {
            slices = 4;  // Low liquidity
        } else if (book_liquidity < 5000) {
            slices = 3;  // Moderate liquidity
        } else {
            slices = 2;  // Deep book, still slice for safety
        }
        
        // Cap slices for very small orders
        if (total_qty < 0.001) {
            slices = 1;  // Don't slice tiny orders
        }
        
        slices = std::max(1, slices);
        
        // Adaptive spacing based on asset volatility
        uint64_t spacing_ns = (spread_bps > 1.5) ? 300'000 :   // 0.3ms for thin
                              (spread_bps > 1.0) ? 500'000 :   // 0.5ms for moderate
                                                   750'000;    // 0.75ms for deep
        
        return {
            slices,
            total_qty / slices,
            spacing_ns
        };
    }
    
    // Single-shot plan (for deep books or small orders)
    SlicePlan single(double total_qty) const {
        return { 1, total_qty, 0 };
    }
    
    // Conservative plan (max slices)
    SlicePlan conservative(double total_qty) const {
        return { 5, total_qty / 5, 500'000 };
    }
};

}  // namespace Chimera
