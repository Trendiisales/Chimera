#pragma once
#include <unordered_map>
#include "EscalationConfig.hpp"
#include "EscalationDecision.hpp"
#include "EscalationSink.hpp"

namespace chimera {

class TakerEscalationEngine {
public:
    TakerEscalationEngine(const EscalationConfig& cfg,
                          EscalationSink& sink);

    void on_signal(uint64_t causal_id,
                   uint64_t signal_ts_ns,
                   double confidence);

    void on_execution_state(uint64_t causal_id,
                            uint64_t now_ns,
                            uint64_t queue_wait_ns,
                            uint64_t rtt_ns,
                            double volatility);

private:
    struct Track {
        uint64_t signal_ts = 0;
        double confidence = 0.0;
        bool decided = false;
    };

    EscalationConfig m_cfg;
    EscalationSink& m_sink;
    std::unordered_map<uint64_t, Track> m_tracks;
};

}
