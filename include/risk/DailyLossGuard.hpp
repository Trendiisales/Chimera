#pragma once
#include <atomic>

namespace Chimera {

class DailyLossGuard {
public:
    explicit DailyLossGuard(double limit_nzd)
        : limit_(limit_nzd) {}

    void on_fill(double pnl_nzd) {
        double v = daily_pnl_.fetch_add(pnl_nzd) + pnl_nzd;
        if (v <= -limit_) tripped_.store(true);
    }

    bool allow_trading() const {
        return !tripped_.load();
    }

    double daily_pnl() const {
        return daily_pnl_.load();
    }

private:
    const double limit_;
    std::atomic<double> daily_pnl_{0.0};
    std::atomic<bool>   tripped_{false};
};

} // namespace Chimera
