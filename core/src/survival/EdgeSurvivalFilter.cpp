#include "chimera/survival/EdgeSurvivalFilter.hpp"
#include <cmath>

namespace chimera {

EdgeSurvivalFilter::EdgeSurvivalFilter(
    MarketBus& market
) : market_bus(market) {}

void EdgeSurvivalFilter::setMinSurvivalBps(
    double bps
) {
    min_survival_bps = bps;
}

void EdgeSurvivalFilter::setFeeModel(
    const FeeModel& f
) {
    fees = f;
}

double EdgeSurvivalFilter::estimateSlippageBps(
    const std::string& symbol,
    double qty
) const {
    double vol = market_bus.volatility(symbol);
    if (vol <= 0.0) return 0.0;

    return std::min(5.0, vol * qty * 0.1);
}

double EdgeSurvivalFilter::estimateLatencyBps(
    const std::string& symbol,
    double latency_ms
) const {
    double vol = market_bus.volatility(symbol);
    return vol * latency_ms * 0.01;
}

double EdgeSurvivalFilter::estimateFundingBps(
    const std::string&
) const {
    return 0.2;
}

SurvivalDecision EdgeSurvivalFilter::evaluate(
    const std::string& symbol,
    bool is_maker,
    double expected_edge_bps,
    double qty,
    double latency_ms
) {
    SurvivalDecision d;

    double spread_bps =
        (market_bus.spread(symbol) /
         market_bus.last(symbol)) * 10000.0;

    double fee_bps =
        is_maker ? fees.maker_bps : fees.taker_bps;

    double slippage_bps =
        estimateSlippageBps(symbol, qty);

    double latency_bps =
        estimateLatencyBps(symbol, latency_ms);

    double funding_bps =
        estimateFundingBps(symbol);

    double cost_bps =
        spread_bps +
        fee_bps +
        slippage_bps +
        latency_bps +
        funding_bps;

    double net_bps =
        expected_edge_bps - cost_bps;

    d.expected_bps = net_bps;
    d.cost_bps = cost_bps;

    if (net_bps >= min_survival_bps) {
        d.allowed = true;
    } else {
        d.allowed = false;
        d.block_reason = "EDGE_SURVIVAL_FAIL";
    }

    return d;
}

}
