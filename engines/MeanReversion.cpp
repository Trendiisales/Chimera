#include "MeanReversion.hpp"

namespace chimera {

MeanReversion::MeanReversion() : engine_id_("MEAN_REV"), sum_(0.0) {}

const std::string& MeanReversion::id() const {
    return engine_id_;
}

void MeanReversion::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    double mid = (tick.bid + tick.ask) * 0.5;

    window_.push_back(mid);
    sum_ += mid;

    if (window_.size() > 20) {
        sum_ -= window_.front();
        window_.pop_front();
    }

    if (window_.size() < 20) return;

    double mean = sum_ / window_.size();
    double diff = mid - mean;

    if (diff > 3.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.01;
        out.push_back(o);
    }

    if (diff < -3.0) {
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
