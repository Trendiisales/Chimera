#pragma once
#include <string>
#include <cstdint>
#include <map>
#include <deque>
#include "core/contract.hpp"

namespace chimera {

// ImpulseReversion - Detects rapid price moves and fades them
// Scoped to BTCUSDT to avoid alpha collision with ETHFade/SOLFade.
// 12bps edge, 0.5x size multiplier, mean reversion on impulse moves.
class ImpulseReversion : public IEngine {
public:
    ImpulseReversion();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    uint64_t last_submit_ns_;
    
    // Per-symbol state for impulse detection
    struct SymbolState {
        std::deque<double> price_window;
        double window_sum = 0.0;
        uint64_t last_impulse_ns = 0;
    };
    std::map<std::string, SymbolState> state_;
    
    static constexpr double MAX_POS = 0.05;
    static constexpr double BASE_QTY = 0.01;
    static constexpr double EDGE_BPS = 12.0;
    static constexpr double INV_K = 0.4;
    static constexpr uint64_t THROTTLE_NS = 25'000'000ULL;  // 25ms throttle
    static constexpr size_t WINDOW_SIZE = 10;
    static constexpr double IMPULSE_THRESHOLD_BPS = 25.0;  // Detect 25bps moves
    static constexpr uint64_t IMPULSE_COOLDOWN_NS = 200'000'000ULL;  // 200ms between impulses
};

}
