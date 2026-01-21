#pragma once
#include <vector>
#include <functional>
#include <string>

namespace chimera {
namespace hardening {

// True Shapley value computation (subset-based, not leave-one-out)
class ShapleyEngine {
public:
    // Compute Shapley value for signal at 'index'
    // f: function that evaluates PnL given a signal mask
    double computeShapley(
        int n_signals,
        int index,
        const std::function<double(const std::vector<bool>&)>& f
    );

private:
    std::vector<std::vector<bool>> generateSubsets(int n, int exclude);
};

}} // namespace chimera::hardening
