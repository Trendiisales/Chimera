#include "SessionBiasEngine.hpp"

namespace chimera {

SessionBiasEngine::SessionBiasEngine(const SessionBiasConfig& cfg)
    : m_cfg(cfg) {}

void SessionBiasEngine::on_trade_result(uint64_t ts_ns,
                                        bool win,
                                        double) {
    uint64_t bucket = ts_ns / m_cfg.bucket_ns;
    Bucket& b = m_buckets[bucket];

    if (win)
        b.wins++;
    else
        b.losses++;
}

SessionBiasState SessionBiasEngine::state(uint64_t now_ns) const {
    SessionBiasState s{};
    uint64_t bucket = now_ns / m_cfg.bucket_ns;
    s.bucket_start_ns = bucket * m_cfg.bucket_ns;

    auto it = m_buckets.find(bucket);
    if (it == m_buckets.end())
        return s;

    const Bucket& b = it->second;
    int total = b.wins + b.losses;
    if (total < 3)
        return s;

    double winrate = double(b.wins) / total;

    if (winrate >= m_cfg.good_threshold)
        s.multiplier = m_cfg.max_upscale;
    else if (winrate <= m_cfg.bad_threshold)
        s.multiplier = m_cfg.max_downscale;

    return s;
}

}
