#pragma once
#include <string>
#include <unordered_map>

namespace chimera {

class FundingEngine {
public:
    FundingEngine();

    void set_threshold(double bps_per_hour);
    void update(const std::string& symbol, double bps_per_hour);

    bool is_hostile_long(const std::string& symbol) const;
    bool is_hostile_short(const std::string& symbol) const;

    double rate(const std::string& symbol) const;

private:
    double m_thresh;
    std::unordered_map<std::string, double> m_rates;
};

}
