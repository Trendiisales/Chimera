#pragma once

#include <cstdint>
#include <string_view>

namespace chimera {

// FNV-1a 32-bit hash - deterministic, fast, cross-platform stable
// Suitable for <1000 symbols with negligible collision risk
static inline uint32_t fnv1a_32(std::string_view s) {
    uint32_t hash = 0x811C9DC5u;  // FNV offset basis
    for (char c : s) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x01000193u;  // FNV prime
    }
    return hash;
}

} // namespace chimera
