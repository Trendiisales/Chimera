#include "control/FundingEngine.hpp"

namespace chimera {

FundingEngine::FundingEngine()
    : m_thresh(5.0) {}  // Block if funding > 5bps/hour

void FundingEngine::set_threshold(double bps_per_hour) {
    m_thresh = bps_per_hour;
}

void FundingEngine::update(const std::string& symbol, double bps_per_hour) {
    m_rates[symbol] = bps_per_hour;
}

double FundingEngine::rate(const std::string& symbol) const {
    auto it = m_rates.find(symbol);
    if (it == m_rates.end())
        return 0.0;
    return it->second;
}

bool FundingEngine::is_hostile_long(const std::string& symbol) const {
    return rate(symbol) > m_thresh;
}

bool FundingEngine::is_hostile_short(const std::string& symbol) const {
    return rate(symbol) < -m_thresh;
}

}
