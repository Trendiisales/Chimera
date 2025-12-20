#pragma once
#include <cstdint>

namespace Chimera {

// Call these from BOTH live and replay paths
void shadow_on_decision_live(uint64_t seq, bool allow, double size_mult);
void shadow_on_decision_replay(uint64_t seq, bool allow, double size_mult);

// Call once at shutdown
void shadow_finish();

}
