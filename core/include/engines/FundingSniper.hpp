#pragma once
#include <cstdint>

namespace chimera {

class FundingSniper {
public:
    FundingSniper();
    
    void update(double funding_rate, uint64_t next_funding_ts_us);
    bool shouldFire(uint64_t now_us) const;
    bool isBuy() const;
    double sizeBias() const;

private:
    double rate_;
    uint64_t next_funding_us_;
};

} // namespace chimera
