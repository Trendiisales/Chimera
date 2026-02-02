#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <sstream>
#include "state/PositionState.hpp"

namespace chimera {

class GuiBroadcaster {
public:
    explicit GuiBroadcaster(PositionState& ps);

    std::string snapshotJSON();
    void onTick(uint64_t ts_ns);

private:
    PositionState& m_positions;
    std::atomic<uint64_t> m_ticks;
    std::mutex m_lock;
    std::string m_cache;
};

}
