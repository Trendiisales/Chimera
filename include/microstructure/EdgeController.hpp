// ═══════════════════════════════════════════════════════════════════════════════
// include/microstructure/EdgeController.hpp
// v4.2.2: Dynamic edge promotion/demotion based on performance
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot for returning stats (copyable)
// ─────────────────────────────────────────────────────────────────────────────
struct EdgeStatsSnapshot {
    int fired = 0;
    int wins = 0;
    
    double score() const {
        return fired > 0 ? double(wins) / fired : 0.5;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-edge statistics (thread-safe atomics)
// ─────────────────────────────────────────────────────────────────────────────
struct EdgeStats {
    std::atomic<int> fired{0};
    std::atomic<int> wins{0};
    
    double score() const {
        int f = fired.load();
        return f > 0 ? double(wins.load()) / f : 0.5;  // Default to neutral
    }
    
    void record(bool win) {
        fired++;
        if (win) wins++;
    }
    
    void reset() {
        fired = 0;
        wins = 0;
    }
    
    EdgeStatsSnapshot snapshot() const {
        return { fired.load(), wins.load() };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Edge controller - adapts edge weights based on performance
// ─────────────────────────────────────────────────────────────────────────────
class EdgeController {
public:
    EdgeController() = default;
    
    // Get weight multiplier for an edge (0.7 to 1.3)
    double weight(const std::string& edge_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stats_.find(edge_name);
        if (it == stats_.end())
            return 1.0;  // Unknown edges get neutral weight
        
        double s = it->second.score();
        
        // Only adjust after sufficient samples
        if (it->second.fired.load() < 10)
            return 1.0;
        
        // Promote/demote based on win rate
        if (s > 0.60) return 1.3;  // Strong performer
        if (s > 0.55) return 1.15; // Good performer
        if (s < 0.35) return 0.5;  // Poor performer - demote heavily
        if (s < 0.40) return 0.7;  // Below average
        return 1.0;  // Neutral
    }
    
    // Record edge contribution to a trade outcome
    void record(const std::string& edge_name, bool win) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_[edge_name].record(win);
    }
    
    // Record multiple edges that contributed to a trade
    void record_trade(const std::vector<std::string>& edges, bool win) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : edges) {
            stats_[e].record(win);
        }
    }
    
    // Get current score for logging
    double score(const std::string& edge_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stats_.find(edge_name);
        if (it == stats_.end())
            return 0.5;
        return it->second.score();
    }
    
    // Get all stats for dashboard
    std::unordered_map<std::string, EdgeStatsSnapshot> all_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_map<std::string, EdgeStatsSnapshot> result;
        for (const auto& [name, stat] : stats_) {
            result[name] = stat.snapshot();
        }
        return result;
    }
    
    // Nightly reset
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, stat] : stats_) {
            stat.reset();
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, EdgeStats> stats_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Edge names (constants for consistency)
// ─────────────────────────────────────────────────────────────────────────────
namespace EdgeNames {
    constexpr const char* BASE_DISPLACEMENT = "base_displacement";
    constexpr const char* QUEUE_DYNAMICS = "queue_dynamics";
    constexpr const char* IMBALANCE_PERSIST = "imbalance_persist";
    constexpr const char* SPREAD_COMPRESSION = "spread_compression";
    constexpr const char* LIQUIDITY_ABSORPTION = "liquidity_absorption";
    constexpr const char* MOMENTUM = "momentum";
    constexpr const char* VOL_BURST = "vol_burst";
}

} // namespace Chimera
