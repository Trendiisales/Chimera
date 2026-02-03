#pragma once
#include <unordered_map>
#include <string>

namespace chimera {

// BUG 4 FIX: Header was not provided in Drop 4.
// Reconstructed from PositionState.cpp member usage:
//   positions_[symbol].qty, .entry_price
//   get() returns Position& out by const ref
struct Position {
    double qty{0.0};
    double entry_price{0.0};
};

class PositionState {
public:
    void update_from_exchange(const std::string& symbol, double qty, double price);
    bool get(const std::string& symbol, Position& out) const;

private:
    std::unordered_map<std::string, Position> positions_;
};

}
