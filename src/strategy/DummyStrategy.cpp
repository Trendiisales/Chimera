#include "strategy/DummyStrategy.hpp"

void DummyStrategy::tick() {
    q_.push(Intent(
        (n_++ % 2 == 0) ? Intent::BUY : Intent::SELL,
        "BTCUSDT",
        1.0
    ));
}
