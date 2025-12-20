#pragma once
#include <atomic>
#include <cstdint>

namespace Chimera {

struct alignas(64) BlindModeState {
    std::atomic<bool> active;
    std::atomic<uint64_t> last_depth_ts_ns;
    std::atomic<uint32_t> miss_count;
    std::atomic<uint32_t> recover_count;

    BlindModeState() {
        active.store(false);
        last_depth_ts_ns.store(0);
        miss_count.store(0);
        recover_count.store(0);
    }
};

class BinanceBlindMode {
public:
    explicit BinanceBlindMode(BlindModeState& state);

    void on_depth_update(uint64_t ts_ns);
    bool should_blind(uint64_t now_ns);
    void on_trade_attempt();

    double widen_price(double px, bool is_bid) const;
    double cap_qty(double qty) const;

private:
    BlindModeState& state_;
};

}
