#pragma once
#include <unordered_map>
#include "SizingConfig.hpp"
#include "SizingDecision.hpp"
#include "SizingSink.hpp"

namespace chimera {

class ConfidenceWeightedSizer {
public:
    ConfidenceWeightedSizer(const SizingConfig& cfg,
                            SizingSink& sink);

    void on_signal(uint64_t causal_id,
                   double confidence);

    void on_execution_feedback(uint64_t causal_id,
                               uint64_t now_ns,
                               uint64_t rtt_ns,
                               uint64_t queue_wait_ns,
                               double slippage_bps,
                               double volatility);

private:
    struct Track {
        double confidence = 0.0;
        bool decided = false;
    };

    double compute_multiplier(const Track& t,
                              uint64_t rtt_ns,
                              uint64_t queue_ns,
                              double slippage_bps,
                              double volatility) const;

    SizingConfig m_cfg;
    SizingSink& m_sink;
    std::unordered_map<uint64_t, Track> m_tracks;
};

}
