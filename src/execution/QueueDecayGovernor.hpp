#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace chimera {

class Context;

// ---------------------------------------------------------------------------
// Queue Decay Governor — adverse selection defense.
//
// Every live order gets a decay clock. As it ages without filling:
//   - Queue position estimate is re-evaluated against current book
//   - If latency is rising, the estimate is less trustworthy → urgency rises
//   - Hard TTL breach (any live order > 5s) → Cancel Federation (system kill)
//
// This sits ABOVE CancelPolicy. CancelPolicy does per-order cleanup based on
// fill probability + timeout. QueueDecayGovernor escalates: when staleness
// becomes systemic (latency-weighted urgency breaches threshold), the system
// is in adverse selection territory and must stop.
//
// LIVE ONLY. Shadow mode: all methods are no-ops.
//
// Threading: all methods called from CORE1 only. No locks needed.
// ---------------------------------------------------------------------------
class QueueDecayGovernor {
public:
    explicit QueueDecayGovernor(Context& ctx);

    void on_order_submitted(const std::string& client_id,
                            const std::string& symbol,
                            double price, bool is_buy);

    void on_order_done(const std::string& client_id);

    void poll();

    void set_hard_ttl_ns(uint64_t ns)    { hard_ttl_ns_ = ns; }
    void set_soft_ttl_ns(uint64_t ns)    { soft_ttl_ns_ = ns; }
    void set_latency_k(double k)         { latency_k_ = k; }
    void set_urgency_threshold(double t) { urgency_threshold_ = t; }

private:
    struct TrackedOrder {
        uint64_t    submit_ns;
        std::string symbol;
        double      price;
        bool        is_buy;
    };

    Context& ctx_;
    std::unordered_map<std::string, TrackedOrder> live_;

    uint64_t hard_ttl_ns_{5'000'000'000ULL};
    uint64_t soft_ttl_ns_{1'000'000'000ULL};
    double   latency_k_{0.002};
    double   urgency_threshold_{12.0};
};

} // namespace chimera
