#pragma once

#include <string>
#include "chimera/execution/PositionBook.hpp"

namespace chimera {

class RiskGovernor {
public:
    bool allowOrder(
        const std::string& symbol,
        double qty,
        double price,
        const PositionBook& book
    );

    void onPnlUpdate(double realized, double unrealized);
    bool killSwitch() const;

    void setDailyLossLimit(double v);
    void setMaxExposure(double v);

private:
    double daily_loss_limit = -0.02;
    double max_exposure = 5.0;
    bool killed = false;
};

}
