#pragma once

#include <atomic>

namespace chimera {
namespace core {

struct ReinforcementState {
    std::atomic<double> hft_reward{0.0};
    std::atomic<double> structure_reward{0.0};
    std::atomic<double> hft_confidence{1.0};
    std::atomic<double> structure_confidence{1.0};
};

enum class EngineType : uint8_t;

class ReinforcementEngine {
public:
    explicit ReinforcementEngine(ReinforcementState& state);

    void update(EngineType engine, double pnl, double drawdown, double latency);
    double get_confidence(EngineType engine) const;

private:
    static constexpr double DECAY_FACTOR = 0.995;  // FIX: Prevents permanent bias
    
    ReinforcementState& m_state;
};

} // namespace core
} // namespace chimera
