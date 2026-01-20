#pragma once
#include <string>

class CorrelationGovernor {
public:
    bool allow(const std::string&) { return true; }
    void onTrade(const std::string&) {}

    double btcStress() const { return 0.0; }
};
