#pragma once
#include <cmath>
#include <cstddef>

class RollingStats {
public:
    explicit RollingStats(size_t window)
        : window_(window) {}

    void reset() {
        n_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
    }

    void push(double x) {
        if (n_ < window_) {
            n_++;
        }

        const double delta = x - mean_;
        mean_ += delta / n_;
        const double delta2 = x - mean_;
        m2_ += delta * delta2;
    }

    double mean() const {
        return mean_;
    }

    double variance() const {
        return (n_ > 1) ? (m2_ / (n_ - 1)) : 0.0;
    }

    double stddev() const {
        return std::sqrt(variance());
    }

    bool ready() const {
        return n_ >= window_;
    }

private:
    size_t window_;
    size_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};
