#include "arbiter/Arbiter.hpp"

using namespace Chimera;

Arbiter::Arbiter(VenueHealth& vh) : vh_(vh) {}

bool Arbiter::allow_execution(uint64_t latency_us) {
    uint64_t ema = vh_.latency_us_ema.load(std::memory_order_relaxed);
    ema = (ema * 7 + latency_us) / 8;
    vh_.latency_us_ema.store(ema, std::memory_order_relaxed);

    if (!vh_.ws_up.load(std::memory_order_relaxed)) {
        vh_.throttled.store(true, std::memory_order_relaxed);
        return false;
    }

    if (ema > 5000 || vh_.reject_rate.load(std::memory_order_relaxed) > 5) {
        vh_.throttled.store(true, std::memory_order_relaxed);
        return false;
    }

    vh_.throttled.store(false, std::memory_order_relaxed);
    return true;
}

void Arbiter::on_reject() {
    vh_.reject_rate.fetch_add(1, std::memory_order_relaxed);
}
