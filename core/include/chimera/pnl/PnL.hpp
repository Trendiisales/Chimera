#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "chimera/infra/Clock.hpp"

namespace chimera::pnl {

struct FillEvent {
    std::string symbol;
    double qty;
    double price;
    double fee;
    infra::MonoTime ts;
};

struct PnLState {
    double realized = 0.0;
    double unrealized = 0.0;
    double fees = 0.0;
    uint64_t fills = 0;
    infra::MonoTime last_update{};
};

class PnLBook {
public:
    void onFill(const FillEvent& f) {
        auto& s = state[f.symbol];
        s.realized += (f.qty * f.price) - f.fee;
        s.fees += f.fee;
        s.fills++;
        s.last_update = f.ts;
    }

    const PnLState& get(const std::string& sym) const {
        static PnLState empty{};
        auto it = state.find(sym);
        return it == state.end() ? empty : it->second;
    }

    const std::unordered_map<std::string, PnLState>& all() const {
        return state;
    }

private:
    std::unordered_map<std::string, PnLState> state;
};

} // namespace chimera::pnl
