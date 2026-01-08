// =============================================================================
// GoldExecutionContract.hpp - EXECUTION TAGGING CONTRACT
// =============================================================================
// Every Gold trade MUST report this on close.
// If this is wrong, scale guard is invalid.
//
// REQUIRED: assert(rpt.valid()) on every Gold trade close
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>

namespace gold {

// =============================================================================
// EXECUTION REPORT (Must be filled on every Gold trade close)
// =============================================================================
struct GoldExecutionReport {
    double pnl_r = 0.0;
    
    bool from_active_campaign = false;  // Was campaign ACTIVE at entry?
    bool partial_taken = false;         // Did we take partial at target?
    bool runner_failed = false;         // Did winner (>=1R) turn into loser?
    
    // -------------------------------------------------------------------------
    // VALIDATION (MUST PASS)
    // -------------------------------------------------------------------------
    [[nodiscard]] bool valid() const {
        // Trade must originate from active campaign
        if (!from_active_campaign) {
            printf("[GOLD-CONTRACT] VIOLATION: Trade not from active campaign\n");
            return false;
        }
        
        // Runner failed is impossible if PnL is positive
        if (runner_failed && pnl_r > 0.0) {
            printf("[GOLD-CONTRACT] VIOLATION: runner_failed but pnl_r > 0\n");
            return false;
        }
        
        return true;
    }
    
    void print() const {
        printf("[GOLD-CONTRACT] Report: pnl=%.2fR campaign=%s partial=%s runner_fail=%s valid=%s\n",
               pnl_r,
               from_active_campaign ? "YES" : "NO",
               partial_taken ? "YES" : "NO",
               runner_failed ? "YES" : "NO",
               valid() ? "YES" : "NO");
    }
};

} // namespace gold
