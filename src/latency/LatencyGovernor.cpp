#include "latency/LatencyGovernor.hpp"

#include <algorithm>
#include <cmath>

LatencyGovernor::LatencyGovernor()
    : count_(0),
      head_(0),
      last_(0.0) {
    samples_.fill(0.0);
}

void LatencyGovernor::record_rtt_ms(double rtt_ms) {
    last_ = rtt_ms;
    samples_[head_] = rtt_ms;
    head_ = (head_ + 1) % WINDOW;
    if (count_ < WINDOW) count_++;
}

double LatencyGovernor::current() const {
    return last_;
}

double LatencyGovernor::percentile(double p) const {
    if (count_ == 0) return 0.0;

    // Copy and sort
    std::array<double, WINDOW> tmp;
    for (int i = 0; i < count_; ++i)
        tmp[i] = samples_[i];

    std::sort(tmp.begin(), tmp.begin() + count_);

    // Calculate index
    int idx = static_cast<int>(std::ceil(p * count_)) - 1;
    if (idx < 0) idx = 0;
    if (idx >= count_) idx = count_ - 1;

    return tmp[idx];
}

double LatencyGovernor::p50() const { return percentile(0.50); }
double LatencyGovernor::p90() const { return percentile(0.90); }
double LatencyGovernor::p95() const { return percentile(0.95); }
double LatencyGovernor::p99() const { return percentile(0.99); }

LatencyRegime LatencyGovernor::regime() const {
    const double p95v = p95();
    const double p99v = p99();
    const double cur  = current();

    // FAST — derived from empirical VPS data
    // p95=8ms from tail analysis, tightened to 6ms for margin
    if (p95v <= 6.0 && p99v <= 12.0 && cur <= 8.0)
        return LatencyRegime::FAST;

    // NORMAL — marginal but usable
    // XAU disabled, XAG continues
    if (p95v <= 10.0 && p99v <= 18.0 && cur <= 14.0)
        return LatencyRegime::NORMAL;

    // DEGRADED — physics says stop
    return LatencyRegime::DEGRADED;
}

bool LatencyGovernor::allow_entry(const std::string& symbol) const {
    LatencyRegime r = regime();

    // XAU: FAST-only (p95 ≤ 6ms is the only acceptable regime)
    if (symbol == "XAUUSD")
        return r == LatencyRegime::FAST;

    // XAG: More tolerant (disabled only in DEGRADED)
    if (symbol == "XAGUSD")
        return r != LatencyRegime::DEGRADED;

    return false;
}

bool LatencyGovernor::allow_time_exit(const std::string& symbol) const {
    // XAU: TIME exits only in FAST regime
    // (latency variance kills expectancy)
    if (symbol == "XAUUSD")
        return regime() == LatencyRegime::FAST;

    // XAG: Always allow TIME exits
    // (deeper book, more forgiving)
    return true;
}
