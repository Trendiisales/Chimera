#pragma once
#include "ReplayLog.hpp"

namespace chimera {

class ReplayRecorder {
public:
    explicit ReplayRecorder(ReplayLog& log);

    void record(ReplayEventType type,
                uint64_t ts_ns,
                uint64_t causal_id,
                const std::string& payload_json);

private:
    ReplayLog& m_log;
};

}
