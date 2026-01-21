#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

namespace chimera {

struct SymbolRules {
    double min_qty = 0.0;
    double step_size = 0.0;
    double tick_size = 0.0;
    double min_notional = 0.0;
};

class ExchangeInfoCache {
public:
    explicit ExchangeInfoCache(const std::string& rest_url);

    void refresh();
    bool has(const std::string& symbol) const;
    const SymbolRules& rules(const std::string& symbol) const;

private:
    void parse(const std::string& json);

private:
    std::string url;
    mutable std::mutex mtx;
    std::unordered_map<std::string, SymbolRules> map;
};

}
