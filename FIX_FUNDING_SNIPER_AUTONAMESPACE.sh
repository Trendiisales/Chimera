#!/usr/bin/env bash
set -e

echo "[CHIMERA] Auto-detecting FundingSniper namespace from header"

HEADER="core/include/engines/FundingSniper.hpp"
CPP="core/src/engines/FundingSniper.cpp"

if [ ! -f "$HEADER" ]; then
  echo "ERROR: $HEADER not found"
  exit 1
fi

# Extract namespace line (supports chimera or chimera::engines)
NS=$(awk '
/^namespace / {
  gsub("namespace","")
  gsub("{","")
  gsub(" ","")
  print
  exit
}' "$HEADER")

if [ -z "$NS" ]; then
  echo "ERROR: Could not detect namespace in header"
  exit 1
fi

echo "[CHIMERA] Detected namespace: $NS"

############################################
# REWRITE CPP TO MATCH HEADER NAMESPACE
############################################

cat > "$CPP" << EOC
#include "engines/FundingSniper.hpp"

namespace $NS {

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

} // namespace $NS
EOC

############################################
# CLEAN + BUILD
############################################

rm -rf build
mkdir build
cd build
cmake ..
make -j2

echo "[CHIMERA] FundingSniper namespace fixed correctly"
