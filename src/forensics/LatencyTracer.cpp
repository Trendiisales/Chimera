#include "forensics/LatencyTracer.hpp"

using namespace chimera;

void LatencyTracer::on_tick(uint64_t id, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mtx_);

    // FIX 3.3: If map is at capacity, evict the entry being overwritten
    // (if any). New entries when at cap are still allowed — they replace
    // or are the start of a new path. The cap is a safety net, not a
    // tight limit. Real eviction happens on ack completion below.
    if (paths_.size() >= MAX_PATHS && paths_.find(id) == paths_.end()) {
        // At cap with a brand new ID — evict the first entry we find.
        // This is O(1) amortized for unordered_map begin().
        paths_.erase(paths_.begin());
    }

    paths_[id].tick_ns = ts;
}

void LatencyTracer::on_decision(uint64_t id, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = paths_.find(id);
    if (it != paths_.end())
        it->second.decision_ns = ts;
}

void LatencyTracer::on_route(uint64_t id, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = paths_.find(id);
    if (it != paths_.end())
        it->second.route_ns = ts;
}

void LatencyTracer::on_ack(uint64_t id, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = paths_.find(id);
    if (it != paths_.end()) {
        it->second.ack_ns = ts;
        // FIX 3.3: Path is complete (tick→decision→route→ack).
        // Caller must have already called get() if they need the full path.
        // Remove now to prevent unbounded growth.
        // NOTE: If caller needs the path AFTER ack, they must call get() first.
        // In practice: the telemetry loop calls get() then on_ack(), or
        // on_ack() writes to telemetry before erasing.
        paths_.erase(it);
    }
}

LatencyPath LatencyTracer::get(uint64_t id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = paths_.find(id);
    if (it == paths_.end()) return LatencyPath{};
    return it->second;
}

size_t LatencyTracer::size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return paths_.size();
}
