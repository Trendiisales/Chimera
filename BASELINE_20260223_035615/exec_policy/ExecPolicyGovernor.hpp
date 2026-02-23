#pragma once
#include <cstdint>
#include "ExecPolicyConfig.hpp"
#include "ExecPolicyState.hpp"
#include "ExecPolicySink.hpp"

namespace chimera {

class ExecPolicyGovernor {
public:
    ExecPolicyGovernor(const ExecPolicyConfig& cfg,
                       ExecPolicySink& sink);

    void on_latency(uint64_t now_ns,
                    uint64_t exchange_rtt_ns,
                    uint64_t queue_wait_ns);

    void on_reject_rate(uint64_t now_ns,
                        double reject_rate);

    void on_market_state(uint64_t now_ns,
                         double spread_bps,
                         double volatility_score);

    void on_exchange_instability(uint64_t now_ns,
                                 bool unstable);

    const ExecPolicyState& state() const;

private:
    void evaluate(uint64_t now_ns);

    ExecPolicyConfig m_cfg;
    ExecPolicySink& m_sink;
    ExecPolicyState m_state;

    uint64_t m_last_hard_kill_ns = 0;

    uint64_t m_rtt_ns = 0;
    uint64_t m_queue_ns = 0;
    double   m_reject_rate = 0.0;
    double   m_spread_bps = 0.0;
    double   m_volatility = 0.0;
    bool     m_exchange_unstable = false;
};

}
