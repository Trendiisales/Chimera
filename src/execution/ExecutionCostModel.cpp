#include "execution/ExecutionCostModel.hpp"

ExecutionCostModel::ExecutionCostModel(double slippage_bps,
                                       double fee_bps)
: slip_(slippage_bps / 10000.0),
  fee_(fee_bps / 10000.0) {}

double ExecutionCostModel::apply_buy(double price) const {
    return price * (1.0 + slip_ + fee_);
}

double ExecutionCostModel::apply_sell(double price) const {
    return price * (1.0 - slip_ - fee_);
}
