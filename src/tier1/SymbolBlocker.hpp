#pragma once
#include <cstdint>
#include <array>

namespace chimera {

struct SymbolBlocker {
    static constexpr uint32_t MAX = 16;

    alignas(64) std::array<uint64_t, MAX> blocked_until_ns;

    SymbolBlocker() {
        for (auto& v : blocked_until_ns) v = 0;
    }

    inline bool allowed(uint32_t id, uint64_t now) const {
        return now >= blocked_until_ns[id];
    }

    inline void block(uint32_t id, uint64_t until) {
        blocked_until_ns[id] = until;
    }
};

}
