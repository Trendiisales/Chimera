#pragma once

#include "chimera/mode/RunMode.hpp"
#include <stdexcept>
#include <string>

namespace chimera::mode {

// Enforce that we're NOT in replay mode
// Use this to guard live-only operations
inline void enforce_not_replay(const char* operation) {
    if (ModeGuard::is_replay()) {
        throw std::runtime_error(
            std::string("FORBIDDEN: ") + operation + 
            " is not allowed in REPLAY mode"
        );
    }
}

// Enforce that we ARE in replay mode
// Use this to guard replay-only operations
inline void enforce_replay(const char* operation) {
    if (!ModeGuard::is_replay()) {
        throw std::runtime_error(
            std::string("FORBIDDEN: ") + operation + 
            " requires REPLAY mode"
        );
    }
}

// Enforce live mode
inline void enforce_live(const char* operation) {
    if (!ModeGuard::is_live()) {
        throw std::runtime_error(
            std::string("FORBIDDEN: ") + operation + 
            " requires LIVE mode"
        );
    }
}

} // namespace chimera::mode
