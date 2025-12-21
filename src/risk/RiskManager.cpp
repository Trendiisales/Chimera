#include "risk/RiskManager.hpp"

RiskManager::RiskManager(double daily_loss_limit_nzd)
: daily_limit_(daily_loss_limit_nzd) {}

void RiskManager::on_pnl_update(double total_pnl) {
    last_pnl_.store(total_pnl, std::memory_order_relaxed);

    if (!killed_.load(std::memory_order_relaxed) &&
        total_pnl <= -daily_limit_) {
        killed_.store(true, std::memory_order_relaxed);
        reason_ = "DAILY_LOSS_LIMIT_BREACH";
    }
}

bool RiskManager::is_killed() const {
    return killed_.load(std::memory_order_relaxed);
}

const std::string& RiskManager::kill_reason() const {
    return reason_;
}
