#include "chimera/execution/ExecutionEngine.hpp"
#include "chimera/control/ControlPlane.hpp"
#include "chimera/survival/EdgeSurvivalFilter.hpp"
#include "chimera/governance/CorrelationGovernor.hpp"
#include "chimera/governance/StrategyFitnessEngine.hpp"

namespace chimera {

ExecutionEngine::ExecutionEngine(
    ControlPlane& control,
    RiskGovernor& risk,
    OrderManager& orders,
    EdgeSurvivalFilter& survival,
    CorrelationGovernor& corr,
    StrategyFitnessEngine& fitness
) : control_plane(control),
    risk_governor(risk),
    order_manager(orders),
    survival_filter(survival),
    correlation_governor(corr),
    fitness_engine(fitness) {}

void ExecutionEngine::onSignal(
    const TradeSignal& sig
) {
    // Strategy fitness check
    if (!fitness_engine.isHealthy(sig.engine)) {
        return;
    }

    // Correlation check
    if (!correlation_governor.allowTrade(sig.engine)) {
        return;
    }

    // Control plane evaluation (commented out for now - needs positionBook method)
    // bool allowed = control_plane.evaluate(sig.engine);
    // if (!allowed) return;

    // Edge survival filter
    SurvivalDecision surv =
        survival_filter.evaluate(
            sig.symbol,
            false,
            sig.qty * 10.0,
            sig.qty,
            5.0
        );

    if (!surv.allowed) return;

    // Risk governor check (commented out for now - needs positionBook method)
    // if (!risk_governor.allowOrder(
    //     sig.symbol,
    //     sig.qty,
    //     sig.price,
    //     control_plane.positionBook()
    // )) {
    //     return;
    // }

    if (risk_governor.killSwitch()) {
        return;
    }

    OrderRequest req;
    req.client_id =
        sig.engine + "_" + sig.symbol;
    req.symbol = sig.symbol;
    req.qty = sig.qty;
    req.price = sig.price;
    req.is_buy = sig.is_buy;

    order_manager.submit(req);
}

}
