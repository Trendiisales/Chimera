#pragma once
#include <string>
#include <cstdint>
#include <map>
#include "core/contract.hpp"
#include "strategy/TrendRegime.hpp"

namespace chimera {

// QueueMarketMaker (QPMM) - Quasi-passive market making on BTC
// Posts quotes near mid, adjusts based on queue position and inventory.
// Maker-only, narrow edge (5bps), small size (0.05x).
// ONLY operates in range-bound markets (trend filter protects from bleeding).
class QueueMarketMaker : public IEngine {
public:
    QueueMarketMaker();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    uint64_t last_submit_ns_;
    
    // Per-symbol state for EMA tracking
    struct SymbolState {
        double ema_mid = 0.0;
        bool initialized = false;
    };
    std::map<std::string, SymbolState> state_;
    
    // Trend filter - QPMM only operates in range-bound markets
    TrendRegime trend_filter_;
    
    static constexpr double MAX_POS = 0.05;
    static constexpr double BASE_QTY = 0.005;  // Small size for quasi-passive
    static constexpr double EDGE_BPS = 5.0;    // Tight edge for market making
    static constexpr double INV_K = 0.25;      // Inventory aversion
    static constexpr uint64_t THROTTLE_NS = 50'000'000ULL;  // 50ms throttle
    static constexpr double EMA_ALPHA = 0.1;   // EMA smoothing factor
};

}
