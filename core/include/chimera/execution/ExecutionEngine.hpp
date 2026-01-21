#pragma once

#include "chimera/execution/OrderManager.hpp"
#include "chimera/execution/RiskGovernor.hpp"

namespace chimera {

struct TradeSignal {
    std::string engine;
    std::string symbol;
    bool is_buy = false;
    double qty = 0.0;
    double price = 0.0;
};

class ControlPlane;
class EdgeSurvivalFilter;
class CorrelationGovernor;
class StrategyFitnessEngine;

class ExecutionEngine {
public:
    ExecutionEngine(
        ControlPlane& control,
        RiskGovernor& risk,
        OrderManager& orders,
        EdgeSurvivalFilter& survival,
        CorrelationGovernor& corr,
        StrategyFitnessEngine& fitness
    );

    void onSignal(const TradeSignal& sig);

private:
    ControlPlane& control_plane;
    RiskGovernor& risk_governor;
    OrderManager& order_manager;
    EdgeSurvivalFilter& survival_filter;
    CorrelationGovernor& correlation_governor;
    StrategyFitnessEngine& fitness_engine;
};

}
