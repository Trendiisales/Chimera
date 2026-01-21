#include "engines/FundingSniper.hpp"

namespace chimera {

FundingSniper::FundingSniper() : rate_(0), next_funding_us_(0) {}

void FundingSniper::update(double funding_rate, uint64_t next_funding_ts_us) {
    rate_ = funding_rate;
    next_funding_us_ = next_funding_ts_us;
}

bool FundingSniper::shouldFire(uint64_t now_us) const {
    if (next_funding_us_ == 0) return false;
    uint64_t time_to_funding = (next_funding_us_ > now_us) ? (next_funding_us_ - now_us) : 0;
    uint64_t window_us = 60 * 1000000;
    return (time_to_funding < window_us) && (rate_ != 0);
}

bool FundingSniper::isBuy() const {
    return rate_ > 0;
}

double FundingSniper::sizeBias() const {
    return (rate_ != 0) ? 1.5 : 1.0;
}

} // namespace chimera
