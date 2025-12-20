#include <cstdint>
#include <chrono>
#include "fix/FixDegradedState.hpp"

namespace Chimera {

static FixDegradedState g_fix_state;

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

class FIXExecutor {
public:
    FIXExecutor() = default;

    void on_connect() {
        g_fix_state.on_connect();
    }

    void on_logon() {
        g_fix_state.on_logon();
    }

    void on_disconnect() {
        g_fix_state.on_disconnect();
    }

    void on_rx_message() {
        g_fix_state.on_rx(now_ns());
    }

    void on_tx_message() {
        g_fix_state.on_tx(now_ns());
    }

    void on_latency(uint64_t latency_us) {
        g_fix_state.on_latency(latency_us);
    }

    void on_reject() {
        g_fix_state.on_reject();
    }

    void on_timeout() {
        g_fix_state.on_timeout();
    }

    void send_order(uint64_t cl_ord_id,
                    double price,
                    double qty,
                    uint8_t side) {
        if (!g_fix_state.allow_new_orders()) {
            return;
        }

        qty *= g_fix_state.size_multiplier();

        fix_send(cl_ord_id, price, qty, side);

        g_fix_state.on_tx(now_ns());
    }

private:
    void fix_send(uint64_t cl_ord_id,
                  double price,
                  double qty,
                  uint8_t side) {
        (void)cl_ord_id;
        (void)price;
        (void)qty;
        (void)side;
    }
};

}
