#pragma once
#include <string>
#include "runtime/ProfitPreset.hpp"

namespace chimera {

struct EVDecision {
    bool use_maker;
    bool allow_taker;
};

class SmartExecutor {
public:
    explicit SmartExecutor(ProfitPreset* preset)
        : m_preset(preset) {}

    EVDecision decide(const std::string&,
                      double edge_bps,
                      double fee_bps,
                      double slip_bps)
    {
        EVDecision d{};
        if (!m_preset) {
            d.use_maker = true;
            d.allow_taker = false;
            return d;
        }
        
        double cost = fee_bps + slip_bps;

        d.use_maker = m_preset->maker_primary;
        d.allow_taker = m_preset->taker_escape && (edge_bps > cost);

        return d;
    }

private:
    ProfitPreset* m_preset;
};

}
