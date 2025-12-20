#pragma once
#include <cstdint>

namespace Chimera {

// Lightweight tap to sit next to the arbiter
void shadow_tap_live(uint64_t seq, bool allow, double size_mult);
void shadow_tap_replay(uint64_t seq, bool allow, double size_mult);

}
