#include "execution/LatencyGovernor.hpp"
#include <chrono>
#include <iostream>

using namespace chimera;

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

void LatencyGovernor::record_submit_ns(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_[client_id] = now_ns();
}

void LatencyGovernor::on_ack(const std::string& client_id) {
    uint64_t ack_ns = now_ns();

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = pending_.find(client_id);
    if (it == pending_.end()) return;  // shadow / unknown — ignore

    uint64_t submit_ns = it->second;
    pending_.erase(it);

    // RTT in microseconds
    uint64_t rtt_us = (ack_ns - submit_ns) / 1000;

    // Sanity: if clock skew produces negative or absurd RTT, ignore.
    // 10s is generous — any real HFT ACK should be <100ms.
    if (rtt_us > 10'000'000) return;

    last_latency_us_.store(rtt_us, std::memory_order_relaxed);

    std::cerr << "[LATENCY] ACK RTT: " << client_id << " " << rtt_us << "us\n";
}

void LatencyGovernor::update_latency_us(uint64_t us) {
    last_latency_us_.store(us, std::memory_order_relaxed);
}

double LatencyGovernor::size_multiplier() const {
    uint64_t us = last_latency_us_.load(std::memory_order_relaxed);

    if (us == 0) return 1.0;      // no measurement = inert
    if (us < 200)   return 1.0;   // normal co-located operation
    if (us < 600)   return 0.5;   // degraded but tradeable
    if (us < 1000)  return 0.25;  // marginal — queue estimates unreliable
    return 0.0;                    // blind — must cancel
}

bool LatencyGovernor::should_cancel_all() const {
    uint64_t us = last_latency_us_.load(std::memory_order_relaxed);
    if (us == 0) return false;
    return us > 1000;  // > 1ms
}
