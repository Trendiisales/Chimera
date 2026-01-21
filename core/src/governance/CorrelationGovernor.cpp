#include "chimera/governance/CorrelationGovernor.hpp"
#include <cmath>

namespace chimera {

CorrelationGovernor::CorrelationGovernor() {}

void CorrelationGovernor::setWindowSize(
    size_t n
) {
    window = n;
}

void CorrelationGovernor::setCorrelationLimit(
    double c
) {
    corr_limit = c;
}

void CorrelationGovernor::recordSample(
    const std::string& engine,
    double pnl
) {
    auto& q = history[engine];
    q.push_back({pnl});
    if (q.size() > window) {
        q.pop_front();
    }
}

double CorrelationGovernor::computeCorrelation(
    const std::vector<double>& a,
    const std::vector<double>& b
) const {
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    // Calculate means
    double mean_a = 0.0;
    double mean_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        mean_a += a[i];
        mean_b += b[i];
    }
    mean_a /= a.size();
    mean_b /= b.size();

    // Calculate correlation coefficient
    double numerator = 0.0;
    double sum_sq_a = 0.0;
    double sum_sq_b = 0.0;

    for (size_t i = 0; i < a.size(); ++i) {
        double dev_a = a[i] - mean_a;
        double dev_b = b[i] - mean_b;
        numerator += dev_a * dev_b;
        sum_sq_a += dev_a * dev_a;
        sum_sq_b += dev_b * dev_b;
    }

    double denominator = std::sqrt(sum_sq_a * sum_sq_b);
    if (denominator == 0.0) return 0.0;
    
    return numerator / denominator;
}

bool CorrelationGovernor::allowTrade(
    const std::string& engine
) const {
    auto it = history.find(engine);
    if (it == history.end()) {
        return true;
    }

    // Need minimum samples for meaningful correlation
    if (it->second.size() < 10) {
        return true;
    }

    // Extract PnL series for this engine
    std::vector<double> base;
    for (const auto& s : it->second) {
        base.push_back(s.pnl);
    }

    // Check correlation with all other engines
    for (const auto& kv : history) {
        if (kv.first == engine) continue;
        if (kv.second.size() < 10) continue;

        // Extract other engine's PnL series
        std::vector<double> other;
        for (const auto& s : kv.second) {
            other.push_back(s.pnl);
        }

        // Align series lengths
        size_t min_len = std::min(base.size(), other.size());
        std::vector<double> base_aligned(
            base.end() - min_len, base.end()
        );
        std::vector<double> other_aligned(
            other.end() - min_len, other.end()
        );

        // Compute correlation
        double corr =
            computeCorrelation(base_aligned, other_aligned);

        // Block if correlation exceeds limit
        if (std::abs(corr) >= corr_limit) {
            return false;
        }
    }

    return true;
}

}
