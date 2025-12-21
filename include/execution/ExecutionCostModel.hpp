#pragma once

class ExecutionCostModel {
public:
    ExecutionCostModel(double slippage_bps,
                       double fee_bps);

    double apply_buy(double price) const;
    double apply_sell(double price) const;

private:
    double slip_;
    double fee_;
};
