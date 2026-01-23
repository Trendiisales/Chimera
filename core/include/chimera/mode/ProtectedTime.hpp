#pragma once

#include "chimera/infra/Clock.hpp"
#include "chimera/mode/Enforcement.hpp"

namespace chimera::mode {

// Protected access to live time
// Throws if called in REPLAY mode
inline infra::MonoTime live_now() {
    enforce_not_replay("live_now()");
    return infra::now();
}

// Safe access to time - returns live or replay time depending on mode
// Replay time should be injected via ReplayBus
inline infra::MonoTime safe_now() {
    // In replay mode, this should never be called
    // All time should come from ReplayBus
    if (ModeGuard::is_replay()) {
        throw std::runtime_error(
            "safe_now() called in REPLAY mode - use ReplayBus timestamps"
        );
    }
    return infra::now();
}

} // namespace chimera::mode
