#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace chimera {

struct SymbolTelemetry {
    double position_qty{0.0};
    double notional{0.0};
    uint64_t last_update_ns{0};
};

class TelemetryState {
public:
    void set_uptime(uint64_t sec);
    void set_latency(uint64_t us);
    void set_drift(bool v);
    void update_symbol(const std::string& sym, double qty, double notional);

    // FIX 2.3: Atomic throttle block counter — eliminates lost-update race.
    // Previously: throttle_blocks() read without mutex, set_throttle_block() wrote
    // with mutex. Two concurrent submits could read the same value, both write val+1,
    // losing one increment.
    // Now: atomic fetch_add(1) is a single indivisible operation. No read-modify-write
    // race possible.
    void increment_throttle_block();
    uint64_t throttle_blocks() const;

    // Fill counter — incremented on each shadow or live fill.
    void increment_fills();
    uint64_t total_fills() const;

    // Risk governor rejections — separate counter from throttle.
    // Previously both risk blocks and throttle blocks incremented the same
    // throttle_blocks_ counter, making the number meaningless (you couldn't
    // tell whether risk or throttle was the bottleneck).
    void increment_risk_block();
    uint64_t risk_blocks() const;

    std::string to_json() const;
    std::string to_prometheus() const;

private:
    mutable std::mutex mtx_;
    uint64_t uptime_sec_{0};
    uint64_t latency_us_{0};
    bool drift_{false};

    // FIX 2.3: atomic — lock-free increment, no mutex needed for this counter.
    std::atomic<uint64_t> throttle_blocks_{0};
    std::atomic<uint64_t> risk_blocks_{0};
    std::atomic<uint64_t> total_fills_{0};

    std::unordered_map<std::string, SymbolTelemetry> symbols_;
};

}
