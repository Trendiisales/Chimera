#pragma once
#include <unordered_map>
#include <string>
#include <cmath>
#include <mutex>
#include <tuple>
#include "router/SymbolState.hpp"

namespace chimera {

class USDPositionGate {
public:
    void set_cap(const std::string& sym, double usd_cap) {
        std::lock_guard<std::mutex> lock(mtx_);
        caps_[sym] = usd_cap;
    }

    void register_symbol(const std::string& sym) {
        std::lock_guard<std::mutex> lock(mtx_);
        states_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(sym),
                       std::forward_as_tuple());
    }

    USDSymbolState& state(const std::string& sym) {
        return states_.at(sym);
    }

    bool allow(const std::string& sym,
               double delta_qty,
               double price,
               uint64_t now_ns,
               uint64_t suppress_ns)
    {
        auto& st = states_.at(sym);

        // CHECK SUPPRESS FIRST (document 9 fix)
        if (st.is_suppressed(now_ns)) {
            return false;
        }

        double cur_pos = st.position.load(std::memory_order_relaxed);
        double next_pos = cur_pos + delta_qty;
        double exposure = std::abs(next_pos * price);
        
        std::lock_guard<std::mutex> lock(mtx_);
        double cap = caps_[sym];

        if (exposure >= cap) {
            st.suppress_until_ns.store(now_ns + suppress_ns,
                                       std::memory_order_relaxed);
            return false;
        }

        return true;
    }

private:
    std::unordered_map<std::string, double> caps_;
    std::unordered_map<std::string, USDSymbolState> states_;
    std::mutex mtx_;
};

}
