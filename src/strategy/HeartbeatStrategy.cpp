#include "strategy/HeartbeatStrategy.hpp"

void HeartbeatStrategy::tick() {
    if ((++n_ % 10) == 0) {
        q_.push(Intent(Intent::BUY, "BTCUSDT", 1.0));
    }
}
