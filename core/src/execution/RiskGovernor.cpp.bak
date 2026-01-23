#include "chimera/execution/RiskGovernor.hpp"

namespace chimera {

bool RiskGovernor::allowOrder(
    const std::string& symbol,
    double qty,
    double,
    const PositionBook& book
) {
    if (killed) return false;

    double exposure = book.totalExposure();
    if (exposure + qty > max_exposure) {
        return false;
    }

    return true;
}

void RiskGovernor::onPnlUpdate(
    double realized,
    double unrealized
) {
    double total = realized + unrealized;
    if (total <= daily_loss_limit) {
        killed = true;
    }
}

bool RiskGovernor::killSwitch() const {
    return killed;
}

void RiskGovernor::setDailyLossLimit(double v) {
    daily_loss_limit = v;
}

void RiskGovernor::setMaxExposure(double v) {
    max_exposure = v;
}

}
