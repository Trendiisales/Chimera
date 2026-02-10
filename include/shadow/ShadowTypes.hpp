#pragma once

#include <cstdint>

namespace shadow {

enum class Side { 
    BUY, 
    SELL, 
    FLAT 
};

enum class ExecMode { 
    SHADOW,  // Simulated execution
    LIVE     // Real FIX orders
};

enum class Regime { 
    NORMAL,   // Can enter new positions
    COOLDOWN, // Waiting after exit
    BLOCKED   // Risk governor override
};

enum class TradeState {
    FLAT,       // No position
    OPEN,       // Base position entered
    PYRAMIDING, // Adding legs
    EXITING,    // Exit in progress
    COOLDOWN    // Post-exit cooldown
};

enum class ExitReason {
    NONE,           // No exit
    STOP,           // Stop loss hit
    STOP_LOSS,      // Stop loss (alias)
    TAKE_PROFIT,    // Target reached
    REVERSAL,       // Reversal detected
    INVALIDATION,   // Signal invalidated
    RISK_LIMIT,     // Risk limit breached
    RANGE_FAILURE,  // Range failure exit
    TIME_STOP,      // Time stop
    PARTIAL_TAKE,   // Partial exit
    TRAIL_STOP      // Trailing stop
};

struct Tick {
    double bid;
    double ask;
    uint64_t ts_ms;
};

struct Signal {
    Side side;
    double price;
    double confidence;
    double raw_momentum;  // Raw momentum (will be normalized by executor using ATR)
};

struct Leg {
    Side side;
    double entry;
    double size;
    double stop;
    uint64_t entry_ts;
    
    double mae = 0.0;  // Maximum Adverse Excursion
    double mfe = 0.0;  // Maximum Favorable Excursion
};

// Position ledger (Document 8: "Shadow must maintain position ledger")
struct Position {
    Side side = Side::FLAT;
    int legs = 0;
    double total_size = 0.0;
    double avg_price = 0.0;
    double stop_price = 0.0;
    uint64_t entry_ts = 0;
    
    double unrealized_pnl = 0.0;
    double mae = 0.0;  // Position MAE
    double mfe = 0.0;  // Position MFE
    
    // FIX-accurate exit state (Document 4-5)
    int reversal_ticks = 0;      // Reversal confirmation counter
    double trailing_stop = 0.0;  // Trailing stop price (0 = inactive)
    bool partial1_done = false;  // First partial taken at 0.3R
    bool partial2_done = false;  // Second partial taken at 0.8R
    double last_momentum = 0.0;  // For slope calculation
};

} // namespace shadow
