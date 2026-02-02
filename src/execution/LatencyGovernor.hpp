#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

namespace chimera {

// ---------------------------------------------------------------------------
// Latency-aware sizing + cancel governor — LIVE PATH ONLY.
//
// When network latency degrades, queue position estimates become stale.
// Stale decisions on a slow link = guaranteed adverse selection. This component
// scales order size down as latency rises, and signals cancel-all when latency
// exceeds the hard threshold.
//
// LATENCY SOURCE: Order ACK latency (submit → NEW ack on user stream).
//   - record_submit_ns(client_id): called on CORE1 when order enters REST.
//   - on_ack(client_id):           called on user stream thread when ACK arrives.
//   Computes RTT = ack_time - submit_time. Updates last_latency_us_ atomically.
//   The mutex-protected pending_ map is the only cross-thread state; it is
//   uncontended (insert on CORE1, erase on user stream — different threads,
//   short critical sections).
//
// WHY ORDER ACK, NOT WS PING/PONG:
//   Beast websocket::stream uses sync read (ws.read() blocks). Ping/pong
//   frames are handled transparently by Beast — user code never sees them.
//   Cannot timestamp them without rewriting the Beast read loop.
//   Order ACK latency is the actual end-to-end latency that matters for
//   trading: it includes network + matching engine + rate limit + throttling.
//   This is the "most important trading latency" (your own words).
//
// Safe defaults when no measurement exists:
//   size_multiplier() → 1.0 (no penalty)
//   should_cancel_all() → false (no cancel)
//
// Thresholds tuned for 0.2ms co-located target:
//   <  200µs  → 1.0  (normal HFT operation)
//   200-600µs → 0.5  (degraded, still tradeable)
//   600µs-1ms → 0.25 (marginal, estimates unreliable)
//   >  1ms    → 0.0  + cancel all (blind territory)
//
// Threading:
//   record_submit_ns() — CORE1 (ExecutionRouter live_submit)
//   on_ack()           — user stream thread (BinanceWSUser NEW event)
//   size_multiplier() / should_cancel_all() / last_latency_us() — CORE1
//   pending_ map protected by mtx_ for cross-thread safety.
//   last_latency_us_ is a relaxed atomic — ordering between update and
//   sizing decision doesn't matter at this granularity.
// ---------------------------------------------------------------------------
class LatencyGovernor {
public:
    // --- Latency measurement (order ACK path) ---

    // Record submit timestamp for an order. Called from CORE1 live_submit().
    void record_submit_ns(const std::string& client_id);

    // Order ACK received. Compute RTT, update latency. Called from user stream.
    // If client_id is unknown (not recorded — e.g. shadow mode), silently ignored.
    void on_ack(const std::string& client_id);

    // --- Legacy direct update (for manual/test injection) ---
    void update_latency_us(uint64_t us);

    // --- Sizing + cancel signals ---

    double size_multiplier() const;
    bool   should_cancel_all() const;

    // Raw latency read — for QueueDecayGovernor urgency math, EdgeAttribution.
    // Returns 0 if no measurement has been made yet.
    uint64_t last_latency_us() const {
        return last_latency_us_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> last_latency_us_{0};  // 0 = no measurement yet

    // Pending submit timestamps — keyed by client_id.
    // Insert: CORE1. Erase: user stream thread. Mutex protects.
    std::unordered_map<std::string, uint64_t> pending_;
    std::mutex mtx_;
};

} // namespace chimera
