#pragma once
#include <array>

class LatencyMonitor {
public:
    static constexpr int WINDOW = 2048;

    LatencyMonitor();

    void record(double rtt_ms);

    double current() const;
    double ewma() const;

    double p95() const;
    double p99() const;

private:
    std::array<double, WINDOW> buf_;
    int head_;
    int count_;

    double last_;
    double ewma_;

    double percentile(double p) const;
};
