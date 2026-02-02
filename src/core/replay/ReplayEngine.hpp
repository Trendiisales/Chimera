#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "state/PositionState.hpp"

namespace chimera {

struct ReplayEvent {
    uint64_t id;
    uint64_t ts_ns;
    std::string type;
    std::string payload;
};

class ReplayEngine {
public:
    explicit ReplayEngine(PositionState& ps);

    // Load semantic stream (JSONL), not just binary index
    void loadJournal(const std::string& path);

    void onEvent(const std::function<void(const ReplayEvent&)>& cb);
    void run();

private:
    std::vector<ReplayEvent> m_events;
    PositionState& m_positions;
    std::function<void(const ReplayEvent&)> m_cb;

    void apply(const ReplayEvent& ev);
};

}
