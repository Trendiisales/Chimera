#pragma once
#include "BinanceShard.hpp"
#include <vector>
#include <memory>
#include <string>

namespace binance {

/*
 Deterministic symbol → shard mapping.
 Shards are heap-owned because they are non-movable
 (atomic + thread members).
*/
class ShardManager {
    std::vector<std::unique_ptr<Shard>> shards;

    static size_t hash_sym(const std::string& s) {
        size_t h = 1469598103934665603ULL;
        for (char c : s)
            h = (h ^ static_cast<size_t>(c)) * 1099511628211ULL;
        return h;
    }

public:
    explicit ShardManager(size_t n) {
        shards.reserve(n);
        for (size_t i = 0; i < n; ++i)
            shards.emplace_back(std::make_unique<Shard>());
    }

    void start() {
        for (size_t i = 0; i < shards.size(); ++i)
            shards[i]->start(static_cast<int>(i));
    }

    void stop() {
        for (auto& s : shards)
            s->stop();
    }

    void route(const DepthDelta& d) {
        size_t idx = hash_sym(d.symbol) % shards.size();
        shards[idx]->push(d);
    }
};

}
