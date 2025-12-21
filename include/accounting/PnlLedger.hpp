#pragma once

#include <atomic>
#include "execution/Fill.hpp"

class PnlLedger {
public:
    void on_fill(const Fill& f) {
        double cur = realized_.load(std::memory_order_relaxed);
        realized_.store(cur + (f.qty * f.price),
                        std::memory_order_relaxed);
    }

    double realized() const {
        return realized_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<double> realized_{0.0};
};
