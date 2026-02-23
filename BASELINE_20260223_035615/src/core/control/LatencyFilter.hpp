#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include "LatencyTypes.hpp"

namespace chimera {

struct LatencySample {
    uint64_t rx_ts_ns;
    uint64_t decision_ts_ns;
    uint64_t send_ts_ns;
    uint64_t ack_ts_ns;
};

class LatencyFilter {
public:
    LatencyFilter();

    void push(const LatencySample& s);
    LatencyState state() const;

    double rtt_avg_ms() const;
    double rtt_p95_ms() const;
    double jitter_ms() const;

    std::string state_string() const;

private:
    void recompute() const;

    mutable bool m_dirty;
    mutable double m_avg;
    mutable double m_p95;
    mutable double m_jitter;

    std::deque<double> m_rtts;
};

}
