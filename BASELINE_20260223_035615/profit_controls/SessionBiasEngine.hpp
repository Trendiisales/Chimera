#pragma once
#include <unordered_map>
#include "SessionBiasConfig.hpp"
#include "SessionBiasState.hpp"

namespace chimera {

class SessionBiasEngine {
public:
    explicit SessionBiasEngine(const SessionBiasConfig& cfg);

    void on_trade_result(uint64_t ts_ns,
                         bool win,
                         double slippage_bps);

    SessionBiasState state(uint64_t now_ns) const;

private:
    SessionBiasConfig m_cfg;

    struct Bucket {
        int wins = 0;
        int losses = 0;
    };

    std::unordered_map<uint64_t, Bucket> m_buckets;
};

}
