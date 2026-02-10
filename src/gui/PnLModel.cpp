#include "PnLModel.hpp"

double shadowToCashPnL(
    const std::string&,
    double qty,
    double entry_px,
    double exit_px,
    const CostModel& cost
) {
    double gross = (exit_px - entry_px) * qty;
    double spread_cost = cost.spread * qty;
    return gross - spread_cost - cost.commission;
}
