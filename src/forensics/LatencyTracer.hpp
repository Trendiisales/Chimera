#pragma once
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace chimera {

struct LatencyPath {
    uint64_t tick_ns{0};
    uint64_t decision_ns{0};
    uint64_t route_ns{0};
    uint64_t ack_ns{0};
};

class LatencyTracer {
public:
    void on_tick(uint64_t id, uint64_t ts);
    void on_decision(uint64_t id, uint64_t ts);
    void on_route(uint64_t id, uint64_t ts);
    void on_ack(uint64_t id, uint64_t ts);
    LatencyPath get(uint64_t id);

    // FIX 3.3: returns number of currently tracked paths (for monitoring)
    size_t size();

private:
    // FIX 3.3: Cap + eviction policy.
    // Previously: paths_ grew without bound. Every tick/decision/route/ack created
    // an entry and completed paths were never removed. Unbounded growth for lifetime
    // of process.
    // Now: on_ack() removes the completed path after recording (caller must call
    // get() before ack if they need the full path). If map exceeds MAX_PATHS
    // (safety cap for memory), oldest entries are evicted.
    static constexpr size_t MAX_PATHS = 10'000;

    std::unordered_map<uint64_t, LatencyPath> paths_;
    std::mutex mtx_;
};

}
