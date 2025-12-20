#pragma once

#include <atomic>
#include <string>
#include <unordered_map>

namespace binance {

struct SymbolMetrics {
    std::atomic<uint64_t> snapshots{0};
    std::atomic<uint64_t> deltas{0};
    std::atomic<uint64_t> gaps{0};
    std::atomic<uint64_t> reconnects{0};
    std::atomic<int> health{0}; // 0=DEAD 1=RED 2=YELLOW 3=GREEN
};

class PrometheusMetrics {
public:
    static PrometheusMetrics& instance();

    SymbolMetrics& for_symbol(const std::string& symbol);

    std::string render() const;

private:
    PrometheusMetrics() = default;

    mutable std::unordered_map<std::string, SymbolMetrics> by_symbol;
};

}
