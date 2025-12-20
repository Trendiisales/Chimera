#include "shadow/ShadowArbiterTap.hpp"
#include "shadow/ShadowHooks.hpp"

using namespace Chimera;

void shadow_tap_live(uint64_t seq, bool allow, double size_mult) {
    shadow_on_decision_live(seq, allow, size_mult);
}

void shadow_tap_replay(uint64_t seq, bool allow, double size_mult) {
    shadow_on_decision_replay(seq, allow, size_mult);
}
