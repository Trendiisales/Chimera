#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>

namespace chimera {

struct ProfitPreset {
    // --- core ---
    std::atomic<uint64_t> suppress_ns;
    std::atomic<uint64_t> max_batch;

    // --- risk ---
    std::atomic<double> max_position;
    std::atomic<double> base_notional;

    // --- ladder ---
    std::atomic<double> scale_step;
    std::atomic<uint64_t> max_layers;

    // --- quality ---
    std::atomic<double> min_queue_score;
    std::atomic<double> min_edge_bps;

    // --- execution ---
    std::atomic<bool> maker_only;
    std::atomic<double> funding_threshold_bps;

    // --- reload timing ---
    std::atomic<uint64_t> last_reload_ns;
    std::atomic<uint64_t> reload_interval_ns;
    
    // --- USD-based capital (Document 4) ---
    std::unordered_map<std::string, double> usd_caps;
    bool maker_primary = true;
    bool taker_escape = true;
    double sigma_floor_bps = 0.5;

    ProfitPreset() {
        // PROFIT-BIASED DEFAULTS (tuned for 2-core VPS + Binance spot)
        suppress_ns.store(350ULL * 1000 * 1000, std::memory_order_relaxed);       // 350ms
        max_batch.store(3, std::memory_order_relaxed);

        max_position.store(0.15, std::memory_order_relaxed);                      // 0.15 BTC/ETH/SOL
        base_notional.store(0.01, std::memory_order_relaxed);

        scale_step.store(1.15, std::memory_order_relaxed);
        max_layers.store(4, std::memory_order_relaxed);

        min_queue_score.store(0.50, std::memory_order_relaxed);                   // Decent queue
        min_edge_bps.store(0.30, std::memory_order_relaxed);                      // Realistic edge

        maker_only.store(false, std::memory_order_relaxed);                       // Hybrid execution
        funding_threshold_bps.store(5.0, std::memory_order_relaxed);

        last_reload_ns.store(0, std::memory_order_relaxed);
        reload_interval_ns.store(5ULL * 1000 * 1000 * 1000, std::memory_order_relaxed);
        
        // USD-based capital allocation (Document 4)
        usd_caps["BTCUSDT"] = 2000.0;
        usd_caps["ETHUSDT"] = 4000.0;
        usd_caps["SOLUSDT"] = 4000.0;
        
        // Log defaults on startup
        std::cout << "[PROFIT_PRESET] DEFAULTS LOADED:\n"
                  << "  suppress_ms=" << (suppress_ns.load() / 1000000) << "\n"
                  << "  max_position=" << max_position.load() << "\n"
                  << "  min_queue_score=" << min_queue_score.load() << "\n"
                  << "  min_edge_bps=" << min_edge_bps.load() << "\n"
                  << "  maker_only=" << (maker_only.load() ? "true" : "false") << "\n"
                  << "  USD caps: BTC=$2000, ETH=$4000, SOL=$4000\n";
    }
    
    double cap_for(const std::string& sym) const {
        auto it = usd_caps.find(sym);
        if (it == usd_caps.end()) return 2000.0;
        return it->second;
    }

    static inline uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    static inline bool getenv_bool(const char* key, bool def) {
        const char* v = std::getenv(key);
        if (!v) return def;
        return (std::strcmp(v, "1") == 0 ||
                std::strcmp(v, "true") == 0 ||
                std::strcmp(v, "TRUE") == 0 ||
                std::strcmp(v, "yes") == 0 ||
                std::strcmp(v, "YES") == 0);
    }

    static inline uint64_t getenv_u64(const char* key, uint64_t def) {
        const char* v = std::getenv(key);
        if (!v) return def;
        return static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
    }

    static inline double getenv_f64(const char* key, double def) {
        const char* v = std::getenv(key);
        if (!v) return def;
        return std::strtod(v, nullptr);
    }

    inline void reload_if_due() {
        uint64_t now = now_ns();
        uint64_t last = last_reload_ns.load(std::memory_order_relaxed);
        uint64_t interval = reload_interval_ns.load(std::memory_order_relaxed);

        if (now - last < interval)
            return;

        last_reload_ns.store(now, std::memory_order_relaxed);
        
        bool any_loaded = false;

        // Core
        uint64_t new_suppress = getenv_u64("SUPPRESS_MS", 350) * 1000ULL * 1000ULL;
        if (new_suppress != suppress_ns.load(std::memory_order_relaxed)) {
            suppress_ns.store(new_suppress, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD suppress_ms=" << (new_suppress / 1000000) << "\n";
            any_loaded = true;
        }

        uint64_t new_batch = getenv_u64("MAX_BATCH", 3);
        if (new_batch != max_batch.load(std::memory_order_relaxed)) {
            max_batch.store(new_batch, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD max_batch=" << new_batch << "\n";
            any_loaded = true;
        }

        // Risk
        double new_pos = getenv_f64("MAX_POSITION", 0.15);
        if (new_pos != max_position.load(std::memory_order_relaxed)) {
            max_position.store(new_pos, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD max_position=" << new_pos << "\n";
            any_loaded = true;
        }

        base_notional.store(
            getenv_f64("BASE_NOTIONAL", 0.01),
            std::memory_order_relaxed
        );

        // Ladder
        scale_step.store(
            getenv_f64("SCALE_STEP", 1.15),
            std::memory_order_relaxed
        );

        max_layers.store(
            getenv_u64("MAX_LAYERS", 4),
            std::memory_order_relaxed
        );

        // Quality
        double new_queue = getenv_f64("MIN_QUEUE_SCORE", 0.50);
        if (new_queue != min_queue_score.load(std::memory_order_relaxed)) {
            min_queue_score.store(new_queue, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD min_queue_score=" << new_queue << "\n";
            any_loaded = true;
        }

        double new_edge = getenv_f64("MIN_EDGE_BPS", 0.30);
        if (new_edge != min_edge_bps.load(std::memory_order_relaxed)) {
            min_edge_bps.store(new_edge, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD min_edge_bps=" << new_edge << "\n";
            any_loaded = true;
        }

        // Execution
        bool new_maker = getenv_bool("MAKER_ONLY", false);
        if (new_maker != maker_only.load(std::memory_order_relaxed)) {
            maker_only.store(new_maker, std::memory_order_relaxed);
            std::cout << "[PROFIT_PRESET] RELOAD maker_only=" << (new_maker ? "true" : "false") << "\n";
            any_loaded = true;
        }

        funding_threshold_bps.store(
            getenv_f64("FUNDING_THRESHOLD_BPS", 5.0),
            std::memory_order_relaxed
        );
        
        if (!any_loaded && last == 0) {
            std::cout << "[PROFIT_PRESET] No .env overrides, using defaults\n";
        }
    }
};

}
