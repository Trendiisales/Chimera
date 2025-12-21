#pragma once
#include "execution/Fill.hpp"

class DailyPnlStore {
public:
    explicit DailyPnlStore(double start = 0.0) : pnl_(start) {}

    void on_fill(const Fill& f);
    double pnl() const { return pnl_; }

private:
    double pnl_;
};
