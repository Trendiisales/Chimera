#include "chimera/infra/LatencyEngine.hpp"

namespace chimera {

uint64_t LatencyEngine::nowMs() const {
    return std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::steady_clock::now()
            .time_since_epoch()
    ).count();
}

void LatencyEngine::onRestSend(
    const std::string& venue,
    uint64_t id
) {
    std::lock_guard<std::mutex> lock(mtx);
    pending[venue][id] = {nowMs()};
}

void LatencyEngine::onRestAck(
    const std::string& venue,
    uint64_t id
) {
    std::lock_guard<std::mutex> lock(mtx);
    auto& pmap = pending[venue];
    auto it = pmap.find(id);
    if (it == pmap.end()) return;

    double rtt =
        static_cast<double>(
            nowMs() - it->second.ts
        );

    LatencyStats& s = data[venue];
    s.rest_rtt_ms =
        0.8 * s.rest_rtt_ms +
        0.2 * rtt;

    pmap.erase(it);
}

void LatencyEngine::onWsTick(
    const std::string& venue,
    uint64_t exchange_ts_ms
) {
    double lag =
        static_cast<double>(
            nowMs() - exchange_ts_ms
        );

    std::lock_guard<std::mutex> lock(mtx);
    LatencyStats& s = data[venue];
    s.ws_lag_ms =
        0.8 * s.ws_lag_ms +
        0.2 * lag;
}

LatencyStats LatencyEngine::stats(
    const std::string& venue
) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = data.find(venue);
    if (it == data.end()) {
        return {};
    }
    return it->second;
}

}
