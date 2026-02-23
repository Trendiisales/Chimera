#include "LatencyFilter.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace chimera {

LatencyFilter::LatencyFilter()
    : m_dirty(true), m_avg(0.0), m_p95(0.0), m_jitter(0.0) {}

void LatencyFilter::push(const LatencySample& s) {
    double rtt = double(s.ack_ts_ns - s.send_ts_ns) / 1e6;
    if (m_rtts.size() > 256) m_rtts.pop_front();
    m_rtts.push_back(rtt);
    m_dirty = true;
}

void LatencyFilter::recompute() const {
    if (!m_dirty || m_rtts.empty()) return;

    double sum = 0.0;
    for (double v : m_rtts) sum += v;
    m_avg = sum / m_rtts.size();

    std::vector<double> tmp(m_rtts.begin(), m_rtts.end());
    std::sort(tmp.begin(), tmp.end());

    size_t idx = size_t(0.95 * tmp.size());
    if (idx >= tmp.size()) idx = tmp.size() - 1;
    m_p95 = tmp[idx];

    double var = 0.0;
    for (double v : tmp) {
        double d = v - m_avg;
        var += d * d;
    }
    m_jitter = std::sqrt(var / tmp.size());
    m_dirty = false;
}

double LatencyFilter::rtt_avg_ms() const {
    recompute();
    return m_avg;
}

double LatencyFilter::rtt_p95_ms() const {
    recompute();
    return m_p95;
}

double LatencyFilter::jitter_ms() const {
    recompute();
    return m_jitter;
}

LatencyState LatencyFilter::state() const {
    recompute();
    if (m_p95 > 3.0 || m_jitter > 1.0) return LatencyState::KILL;
    if (m_avg > 1.5) return LatencyState::DEGRADED;
    return LatencyState::OK;
}

std::string LatencyFilter::state_string() const {
    LatencyState s = state();
    if (s == LatencyState::OK) return "OK";
    if (s == LatencyState::DEGRADED) return "DEGRADED";
    return "KILL";
}

}
