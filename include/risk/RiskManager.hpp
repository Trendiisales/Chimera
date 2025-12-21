#pragma once
#include "execution/Fill.hpp"

class RiskManager {
public:
    explicit RiskManager(double daily_loss_limit)
    : limit_(daily_loss_limit) {}

    void on_fill(const Fill& f);
    bool ok() const { return pnl_ > -limit_; }

private:
    double pnl_ = 0.0;
    double limit_;
};
