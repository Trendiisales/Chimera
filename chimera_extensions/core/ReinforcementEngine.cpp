#include "ReinforcementEngine.hpp"
#include "OrderIntentTypes.hpp"
#include <cmath>
#include <algorithm>

namespace chimera {
namespace core {

ReinforcementEngine::ReinforcementEngine(ReinforcementState& state)
    : m_state(state) {}

void ReinforcementEngine::update(EngineType engine, double pnl, double drawdown, double latency) {
    double reward = pnl - (drawdown * 0.5) - (latency * 0.01);

    if (engine == EngineType::HFT) {
        // FIX: Apply decay to prevent early wins from permanent bias
        double current_reward = m_state.hft_reward.load();
        double new_reward = current_reward * DECAY_FACTOR + reward;
        m_state.hft_reward.store(new_reward);
        
        double confidence = std::clamp(new_reward / 1000.0, 0.2, 2.0);
        m_state.hft_confidence.store(confidence);
    } else {
        double current_reward = m_state.structure_reward.load();
        double new_reward = current_reward * DECAY_FACTOR + reward;
        m_state.structure_reward.store(new_reward);
        
        double confidence = std::clamp(new_reward / 1000.0, 0.2, 2.0);
        m_state.structure_confidence.store(confidence);
    }
}

double ReinforcementEngine::get_confidence(EngineType engine) const {
    if (engine == EngineType::HFT)
        return m_state.hft_confidence.load();
    else
        return m_state.structure_confidence.load();
}

} // namespace core
} // namespace chimera
