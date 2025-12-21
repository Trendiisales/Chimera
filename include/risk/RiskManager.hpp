#pragma once

#include <atomic>
#include <string>

class RiskManager {
public:
    explicit RiskManager(double daily_loss_limit_nzd);

    void on_pnl_update(double total_pnl);
    bool is_killed() const;
    const std::string& kill_reason() const;

private:
    double daily_limit_;

    std::atomic<bool> killed_{false};
    std::atomic<double> last_pnl_{0.0};
    std::string reason_;
};
