#include "MeanReversion.hpp"

namespace chimera {

MeanReversion::MeanReversion() : engine_id_("MEAN_REV") {}

const std::string& MeanReversion::id() const {
    return engine_id_;
}

void MeanReversion::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    double mid = (tick.bid + tick.ask) * 0.5;

    // Per-symbol state — each symbol has its own independent window + sum.
    auto& st = state_[tick.symbol];

    st.window.push_back(mid);
    st.sum += mid;

    if (st.window.size() > 20) {
        st.sum -= st.window.front();
        st.window.pop_front();
    }

    if (st.window.size() < 20) return;

    double mean = st.sum / static_cast<double>(st.window.size());

    // ---------------------------------------------------------------------------
    // Relative deviation in bps. Absolute $3 threshold was noise on BTC ($78k =
    // 0.004%) — fired every tick. 30bps = $23 on BTC, $0.72 on ETH. Fires only
    // on real mean-reversion signals.
    // ---------------------------------------------------------------------------
    double diff_bps = (mid - mean) / mean * 10000.0;

    // ---------------------------------------------------------------------------
    // Position cap: max 0.05 units per symbol per direction.
    // tick.position is injected by StrategyRunner from GlobalRiskGovernor.
    // Positive = long, negative = short. Gate on absolute position.
    // Without this, the engine accumulates unbounded on sustained regime moves.
    // ---------------------------------------------------------------------------
    static constexpr double MAX_POS = 0.05;
    double abs_pos = (tick.position > 0) ? tick.position : -tick.position;
    if (abs_pos >= MAX_POS) return;

    if (diff_bps > 30.0) {
        // Price above mean -> sell to mean-revert.
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.01;
        out.push_back(o);
    }

    if (diff_bps < -30.0) {
        // Price below mean -> buy to mean-revert.
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.01;
        out.push_back(o);
    }
}

}
