#pragma once
#include "CapitalConfig.hpp"
#include "CapitalState.hpp"
#include "CapitalSink.hpp"

namespace chimera {

class CapitalAllocator {
public:
    CapitalAllocator(const CapitalConfig& cfg,
                     CapitalSink& sink);

    void on_pnl_update(uint64_t now_ns,
                       double realized_pnl,
                       double drawdown);

    const CapitalState& state() const;

private:
    void evaluate(uint64_t now_ns);

    CapitalConfig m_cfg;
    CapitalSink& m_sink;
    CapitalState m_state;

    uint64_t m_last_stable_ns = 0;
};

}
