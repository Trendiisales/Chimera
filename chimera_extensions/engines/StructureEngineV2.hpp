#pragma once

#include "IEngine.hpp"
#include "RegimeClassifier.hpp"
#include "../core/ThreadSafeQueue.hpp"
#include "../core/OrderIntentTypes.hpp"
#include <atomic>

namespace chimera {
namespace engines {

class StructureEngineV2 : public IEngine {
public:
    explicit StructureEngineV2(core::ThreadSafeQueue<core::OrderIntent>& output);
    
    void on_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns) override;
    void stop() override;

private:
    core::ThreadSafeQueue<core::OrderIntent>& m_output_queue;
    RegimeClassifier m_regime;
    std::atomic<bool> m_running{true};
};

} // namespace engines
} // namespace chimera
