#include "state/PositionState.hpp"

using namespace chimera;

void PositionState::update_from_exchange(const std::string& symbol,
                                          double qty, double price) {
    positions_[symbol].qty         = qty;
    positions_[symbol].entry_price = price;
}

bool PositionState::get(const std::string& symbol, Position& out) const {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return false;
    out = it->second;
    return true;
}
