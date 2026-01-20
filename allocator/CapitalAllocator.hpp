#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <mutex>

struct BucketScore {
    std::string name;
    double edge = 0;
    double spread = 0;
    double funding = 0;
    double latency = 0;
    double regime = 0;
    double correlation = 0;
    double net = 0;
    double allocation = 0;
};

class CapitalAllocator {
public:
    CapitalAllocator() {
        buckets_ = {
            {"BTC_SPOT", {}},
            {"BTC_PERP", {}},
            {"ETH_SPOT", {}},
            {"ETH_PERP", {}},
            {"SOL_SPOT", {}},
            {"SOL_PERP", {}}
        };
    }

    void updateMetric(const std::string& bucket,
                      double edge,
                      double spread,
                      double funding,
                      double latency,
                      double regime,
                      double corr) {
        std::lock_guard<std::mutex> g(mu_);
        auto& b = buckets_[bucket];
        b.name = bucket;
        b.edge = edge;
        b.spread = spread;
        b.funding = funding;
        b.latency = latency;
        b.regime = regime;
        b.correlation = corr;
        b.net = edge - spread - funding - latency - regime - corr;
    }

    std::vector<BucketScore> rank(double total_capital) {
        std::lock_guard<std::mutex> g(mu_);
        std::vector<BucketScore> out;
        for (auto& it : buckets_) out.push_back(it.second);

        std::sort(out.begin(), out.end(),
                  [](const BucketScore& a, const BucketScore& b) {
                      return a.net > b.net;
                  });

        double sum = 0;
        for (auto& b : out) if (b.net > 0) sum += b.net;

        for (auto& b : out) {
            if (sum > 0 && b.net > 0)
                b.allocation = (b.net / sum) * total_capital;
            else
                b.allocation = 0;
        }
        return out;
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, BucketScore> buckets_;
};
