#pragma once
#include <atomic>
#include <cstdint>

namespace Chimera {

enum class FixState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING   = 1,
    LOGGED_IN    = 2,
    DEGRADED     = 3,
    HALTED       = 4
};

struct alignas(64) FixStateMetrics {
    std::atomic<uint64_t> last_rx_ns;
    std::atomic<uint64_t> last_tx_ns;
    std::atomic<uint32_t> reject_count;
    std::atomic<uint32_t> timeout_count;
    std::atomic<uint64_t> latency_us_ema;

    FixStateMetrics() {
        last_rx_ns.store(0);
        last_tx_ns.store(0);
        reject_count.store(0);
        timeout_count.store(0);
        latency_us_ema.store(0);
    }
};

class FixDegradedState {
public:
    FixDegradedState();

    void on_connect();
    void on_logon();
    void on_disconnect();

    void on_rx(uint64_t now_ns);
    void on_tx(uint64_t now_ns);
    void on_latency(uint64_t latency_us);
    void on_reject();
    void on_timeout();

    FixState state() const;

    bool allow_new_orders() const;
    double size_multiplier() const;

private:
    void update_state();

    std::atomic<FixState> state_;
    FixStateMetrics metrics_;
};

}
