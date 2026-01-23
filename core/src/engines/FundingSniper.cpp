#include "chimera/engines/FundingSniper.hpp"
#include <cmath>

namespace chimera {

FundingSniper::FundingSniper()
    : rate_(0.0)
    , next_funding_us_(0) {
}

void FundingSniper::update(double funding_rate, uint64_t next_funding_ts_us) {
    rate_ = funding_rate;
    next_funding_us_ = next_funding_ts_us;
}

bool FundingSniper::shouldFire(uint64_t now_us) const {
    const uint64_t FIVE_MIN_US = 5 * 60 * 1000000ULL;
    
    if (next_funding_us_ > now_us) {
        uint64_t time_to_funding = next_funding_us_ - now_us;
        return time_to_funding < FIVE_MIN_US && std::abs(rate_) > 0.0001;
    }
    
    return false;
}

bool FundingSniper::isBuy() const {
    return rate_ < 0.0;
}

double FundingSniper::sizeBias() const {
    return std::min(2.0, std::abs(rate_) * 10000.0);
}

} // namespace chimera
