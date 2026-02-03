#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace chimera {

class FundingBias {
public:
    FundingBias() = default;

    void update_funding(const std::string& symbol, double funding_bps) {
        funding_rates_[symbol] = funding_bps;
    }

    bool allow_long(const std::string& symbol) const {
        auto it = funding_rates_.find(symbol);
        if (it == funding_rates_.end()) return true;
        return it->second <= threshold_bps_;
    }

    bool allow_short(const std::string& symbol) const {
        auto it = funding_rates_.find(symbol);
        if (it == funding_rates_.end()) return true;
        return it->second >= -threshold_bps_;
    }

    void set_threshold(double bps) {
        threshold_bps_ = bps;
    }

private:
    double threshold_bps_ = 5.0;  // 5bps/hour default
    std::unordered_map<std::string, double> funding_rates_;
};

}
