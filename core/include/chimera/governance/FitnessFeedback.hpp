#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

namespace chimera::governance {

class FitnessFeedback {
public:
    void update(const std::string& engine, double pnl_bps) {
        std::lock_guard<std::mutex> lock(mtx);
        scores[engine] += pnl_bps;
    }

    double weight(const std::string& engine) const {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = scores.find(engine);
        if (it == scores.end()) return 1.0;
        return it->second > 0 ? it->second : 0.1;
    }
    
    double score(const std::string& engine) const {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = scores.find(engine);
        return it != scores.end() ? it->second : 0.0;
    }

private:
    mutable std::mutex mtx;
    std::unordered_map<std::string, double> scores;
};

}
