#include "FundingBias.hpp"

namespace chimera {

FundingBias::FundingBias() : engine_id_("FUNDING_BIAS"), funding_rate_(0.0) {}

const std::string& FundingBias::id() const {
    return engine_id_;
}

void FundingBias::onRestore() {
    funding_rate_ = 0.0;
}

void FundingBias::onFunding(double rate) {
    funding_rate_ = rate;
}

void FundingBias::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    constexpr double BIAS_THRESH = 0.0005;

    if (funding_rate_ > BIAS_THRESH && tick.position > 0.01) {
        // Positive funding, reduce longs
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.01;
        out.push_back(o);
    } else if (funding_rate_ < -BIAS_THRESH && tick.position < -0.01) {
        // Negative funding, reduce shorts
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
