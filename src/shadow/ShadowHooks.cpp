#include "shadow/ShadowHooks.hpp"
#include "shadow/ShadowRecorder.hpp"

using namespace Chimera;

void shadow_on_decision_live(uint64_t seq, bool allow, double size_mult) {
    shadow_recorder().record(seq, ShadowSource::LIVE, allow, size_mult);
}

void shadow_on_decision_replay(uint64_t seq, bool allow, double size_mult) {
    shadow_recorder().record(seq, ShadowSource::REPLAY, allow, size_mult);
}

void shadow_finish() {
    shadow_recorder().finish();
}
