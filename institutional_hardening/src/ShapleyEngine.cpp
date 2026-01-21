#include "ShapleyEngine.hpp"
#include <cmath>

namespace chimera {
namespace hardening {

std::vector<std::vector<bool>> ShapleyEngine::generateSubsets(int n, int exclude) {
    std::vector<std::vector<bool>> result;
    int subset_count = 1 << (n - 1); // 2^(n-1) subsets

    for (int mask = 0; mask < subset_count; mask++) {
        std::vector<bool> subset(n, false);
        int bit_idx = 0;
        
        for (int i = 0; i < n; i++) {
            if (i == exclude) continue;
            subset[i] = (mask & (1 << bit_idx)) != 0;
            bit_idx++;
        }
        
        result.push_back(subset);
    }
    
    return result;
}

double ShapleyEngine::computeShapley(
    int n_signals,
    int index,
    const std::function<double(const std::vector<bool>&)>& f) {
    
    auto subsets = generateSubsets(n_signals, index);
    double sum = 0.0;
    
    for (const auto& subset : subsets) {
        auto with_signal = subset;
        with_signal[index] = true;
        
        double marginal_contribution = f(with_signal) - f(subset);
        sum += marginal_contribution;
    }
    
    return sum / static_cast<double>(subsets.size());
}

}} // namespace chimera::hardening
