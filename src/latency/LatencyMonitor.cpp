#include "latency/LatencyMonitor.hpp"
#include <algorithm>
#include <cmath>

LatencyMonitor::LatencyMonitor()
    : head_(0), count_(0), last_(0.0), ewma_(0.0) {
    buf_.fill(0.0);
}

void LatencyMonitor::record(double rtt_ms) {
    last_ = rtt_ms;
    ewma_ = (ewma_ == 0.0) ? rtt_ms : (0.8 * ewma_ + 0.2 * rtt_ms);

    buf_[head_] = rtt_ms;
    head_ = (head_ + 1) % WINDOW;
    if (count_ < WINDOW) count_++;
}

double LatencyMonitor::current() const { return last_; }
double LatencyMonitor::ewma() const { return ewma_; }

double LatencyMonitor::percentile(double p) const {
    if (count_ == 0) return 0.0;
    std::array<double, WINDOW> tmp;
    for (int i = 0; i < count_; ++i) tmp[i] = buf_[i];
    std::sort(tmp.begin(), tmp.begin() + count_);
    int idx = static_cast<int>(std::ceil(p * count_)) - 1;
    if (idx < 0) idx = 0;
    if (idx >= count_) idx = count_ - 1;
    return tmp[idx];
}

double LatencyMonitor::p95() const { return percentile(0.95); }
double LatencyMonitor::p99() const { return percentile(0.99); }
