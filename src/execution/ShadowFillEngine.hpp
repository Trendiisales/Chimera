#pragma once
#include <string>
#include "runtime/Context.hpp"

namespace chimera {

class ShadowFillEngine {
public:
    explicit ShadowFillEngine(Context& ctx);
    void on_fill(const std::string& symbol, double qty, double price);

    // Queue-driven fill decision for shadow simulation.
    // Returns true if the order should fill based on current queue position.
    // Uses QueuePositionModel estimate — replaces fixed probability threshold.
    // is_buy: true for buy orders, false for sell.
    bool should_fill(const std::string& symbol, double price, double qty, bool is_buy);

private:
    Context& ctx_;
};

}
