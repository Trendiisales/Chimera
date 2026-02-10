#pragma once
#include <string>

struct CostModel {
    double spread;
    double commission;
};

double shadowToCashPnL(
    const std::string& symbol,
    double qty,
    double entry_px,
    double exit_px,
    const CostModel& cost
);
