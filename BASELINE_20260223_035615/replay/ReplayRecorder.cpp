#include "ReplayRecorder.hpp"

namespace chimera {

ReplayRecorder::ReplayRecorder(ReplayLog& log)
    : m_log(log) {}

void ReplayRecorder::record(ReplayEventType type,
                            uint64_t ts_ns,
                            uint64_t causal_id,
                            const std::string& payload_json) {
    m_log.append({type, ts_ns, causal_id, payload_json});
}

}
