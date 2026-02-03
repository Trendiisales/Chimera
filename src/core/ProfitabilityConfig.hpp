#pragma once
#include <cstdint>

namespace chimera {

// Centralized profitability tuning parameters
struct ProfitabilityConfig {
    // Position caps
    static constexpr double POSITION_CAP = 0.20;
    
    // Risk limits
    static constexpr double STRATEGY_FLOOR = -25.0;
    static constexpr double PORTFOLIO_DD = -200.0;
    
    // Queue decay
    static constexpr uint64_t SOFT_TTL_MS = 250;
    static constexpr uint64_t HARD_TTL_MS = 1500;
    static constexpr double MIN_QUEUE_SCORE = 0.65;
    
    // Symbol suppression
    static constexpr uint64_t SUPPRESS_NS = 500'000'000;  // 500ms
    
    // Funding bias
    static constexpr double FUNDING_THRESHOLD_BPS = 5.0;
    
    // Capital ladder
    static constexpr double BASE_NOTIONAL = 0.01;
    static constexpr double SCALE_STEP = 1.5;
    static constexpr int MAX_LAYERS = 4;
    
    // Batch routing
    static constexpr size_t MAX_BATCH = 8;
    
    // Strategy params
    static constexpr double Z_ENTER = 1.8;
    static constexpr double Z_EXIT = 0.6;
    static constexpr double MIN_EDGE_BPS = 1.2;
    static constexpr double TAKE_PROFIT_BPS = 8.0;
    static constexpr double STOP_LOSS_BPS = 12.0;
};

}
