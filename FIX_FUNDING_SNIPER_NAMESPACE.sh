#!/usr/bin/env bash
set -e

echo "[CHIMERA] Fixing FundingSniper namespace mismatch"

############################################
# FUNDING SNIPER — MATCH HEADER NAMESPACE
############################################

cat > core/src/engines/FundingSniper.cpp << 'EOC'
#include "engines/FundingSniper.hpp"

namespace chimera {

FundingSniper::FundingSniper()
    : funding_rate_(0.0),
      next_funding_ts_us_(0) {}

void FundingSniper::update(double funding_rate, uint64_t next_funding_ts_us) {
    funding_rate_ = funding_rate;
    next_funding_ts_us_ = next_funding_ts_us;
}

bool FundingSniper::shouldFire(uint64_t now_us) const {
    if (next_funding_ts_us_ == 0) return false;

    uint64_t diff = (next_funding_ts_us_ > now_us)
        ? (next_funding_ts_us_ - now_us)
        : 0;

    return diff < 30ULL * 1000ULL * 1000ULL;
}

bool FundingSniper::isBuy() const {
    return funding_rate_ > 0.0;
}

double FundingSniper::sizeBias() const {
    double r = funding_rate_;
    if (r < 0) r = -r;
    return 1.0 + r * 10.0;
}

} // namespace chimera
EOC

############################################
# CLEAN BUILD + REBUILD
############################################

rm -rf build
mkdir build
cd build
cmake ..
make -j2

echo "[CHIMERA] FundingSniper namespace fixed"
