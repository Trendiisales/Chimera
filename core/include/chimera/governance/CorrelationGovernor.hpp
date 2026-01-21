#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <vector>

namespace chimera {

struct PnLSample {
    double pnl = 0.0;
};

class CorrelationGovernor {
public:
    CorrelationGovernor();

    void recordSample(
        const std::string& engine,
        double pnl
    );

    bool allowTrade(
        const std::string& engine
    ) const;

    void setWindowSize(size_t n);
    void setCorrelationLimit(double c);

private:
    double computeCorrelation(
        const std::vector<double>& a,
        const std::vector<double>& b
    ) const;

private:
    std::unordered_map<
        std::string,
        std::deque<PnLSample>
    > history;

    size_t window = 50;
    double corr_limit = 0.85;
};

}
