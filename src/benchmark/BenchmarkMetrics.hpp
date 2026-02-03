#pragma once
#include <chrono>
#include <atomic>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace chimera {

// ---------------------------------------------------------------------------
// BenchmarkMetrics: Standardized metrics for comparing tiers
// 
// Captures:
//   - Fills (successful trades)
//   - Blocks (rejected by position gate)
//   - Backpressure (rejected by cooldown/ring)
//   - Latency (signal generation → fill)
//   - Throughput (signals/sec)
// ---------------------------------------------------------------------------

struct BenchmarkMetrics {
    std::atomic<uint64_t> signals_generated{0};
    std::atomic<uint64_t> signals_submitted{0};
    std::atomic<uint64_t> fills{0};
    std::atomic<uint64_t> blocks_position_cap{0};
    std::atomic<uint64_t> blocks_backpressure{0};
    std::atomic<uint64_t> blocks_cooldown{0};
    
    // Latency tracking (simple min/max/sum for now)
    std::atomic<uint64_t> latency_sum_ns{0};
    std::atomic<uint64_t> latency_min_ns{UINT64_MAX};
    std::atomic<uint64_t> latency_max_ns{0};
    std::atomic<uint64_t> latency_count{0};
    
    uint64_t start_time_ns{0};
    uint64_t end_time_ns{0};
    
    void start() {
        start_time_ns = now_ns();
    }
    
    void stop() {
        end_time_ns = now_ns();
    }
    
    void record_signal_generated() {
        signals_generated.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_signal_submitted() {
        signals_submitted.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_fill(uint64_t signal_ts_ns) {
        fills.fetch_add(1, std::memory_order_relaxed);
        
        // Record latency
        uint64_t now = now_ns();
        uint64_t latency = now - signal_ts_ns;
        
        latency_sum_ns.fetch_add(latency, std::memory_order_relaxed);
        latency_count.fetch_add(1, std::memory_order_relaxed);
        
        // Update min
        uint64_t current_min = latency_min_ns.load(std::memory_order_relaxed);
        while (latency < current_min &&
               !latency_min_ns.compare_exchange_weak(current_min, latency,
                   std::memory_order_relaxed)) {}
        
        // Update max
        uint64_t current_max = latency_max_ns.load(std::memory_order_relaxed);
        while (latency > current_max &&
               !latency_max_ns.compare_exchange_weak(current_max, latency,
                   std::memory_order_relaxed)) {}
    }
    
    void record_block_position_cap() {
        blocks_position_cap.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_block_backpressure() {
        blocks_backpressure.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_block_cooldown() {
        blocks_cooldown.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Print summary
    void print_summary(const std::string& tier_name) const {
        uint64_t duration_ns = end_time_ns - start_time_ns;
        double duration_s = duration_ns / 1e9;
        
        uint64_t sig_gen = signals_generated.load(std::memory_order_relaxed);
        uint64_t sig_sub = signals_submitted.load(std::memory_order_relaxed);
        uint64_t f = fills.load(std::memory_order_relaxed);
        uint64_t b_cap = blocks_position_cap.load(std::memory_order_relaxed);
        uint64_t b_bp = blocks_backpressure.load(std::memory_order_relaxed);
        uint64_t b_cd = blocks_cooldown.load(std::memory_order_relaxed);
        
        uint64_t lat_sum = latency_sum_ns.load(std::memory_order_relaxed);
        uint64_t lat_cnt = latency_count.load(std::memory_order_relaxed);
        uint64_t lat_min = latency_min_ns.load(std::memory_order_relaxed);
        uint64_t lat_max = latency_max_ns.load(std::memory_order_relaxed);
        
        std::cout << "\n";
        std::cout << "========================================\n";
        std::cout << "BENCHMARK RESULTS: " << tier_name << "\n";
        std::cout << "========================================\n";
        std::cout << "\n";
        std::cout << "Duration: " << std::fixed << std::setprecision(2) 
                  << duration_s << " seconds\n";
        std::cout << "\n";
        std::cout << "--- THROUGHPUT ---\n";
        std::cout << "Signals Generated:  " << sig_gen 
                  << " (" << (sig_gen / duration_s) << " /sec)\n";
        std::cout << "Signals Submitted:  " << sig_sub
                  << " (" << (sig_sub / duration_s) << " /sec)\n";
        std::cout << "Fills:              " << f
                  << " (" << (f / duration_s) << " /sec)\n";
        std::cout << "\n";
        std::cout << "--- BLOCKS ---\n";
        std::cout << "Position Cap:       " << b_cap << "\n";
        std::cout << "Backpressure:       " << b_bp << "\n";
        std::cout << "Cooldown:           " << b_cd << "\n";
        std::cout << "Total Blocks:       " << (b_cap + b_bp + b_cd) << "\n";
        std::cout << "\n";
        std::cout << "--- EFFICIENCY ---\n";
        std::cout << "Submit Rate:        " 
                  << std::fixed << std::setprecision(1)
                  << (100.0 * sig_sub / std::max(1UL, sig_gen)) << "%\n";
        std::cout << "Fill Rate:          "
                  << (100.0 * f / std::max(1UL, sig_sub)) << "%\n";
        std::cout << "\n";
        std::cout << "--- LATENCY (signal → fill) ---\n";
        if (lat_cnt > 0) {
            double avg_us = (lat_sum / lat_cnt) / 1000.0;
            double min_us = lat_min / 1000.0;
            double max_us = lat_max / 1000.0;
            
            std::cout << "Min:                " << std::fixed << std::setprecision(2)
                      << min_us << " µs\n";
            std::cout << "Avg:                " << avg_us << " µs\n";
            std::cout << "Max:                " << max_us << " µs\n";
        } else {
            std::cout << "No latency data\n";
        }
        std::cout << "\n";
        std::cout << "========================================\n";
        std::cout << "\n";
    }
    
    // Save to CSV for comparison
    void save_csv(const std::string& filename, const std::string& tier_name) const {
        std::ofstream f(filename, std::ios::app);
        if (!f.is_open()) {
            std::cerr << "Failed to open " << filename << "\n";
            return;
        }
        
        // Check if file is empty (write header)
        f.seekp(0, std::ios::end);
        if (f.tellp() == 0) {
            f << "tier,duration_s,signals_generated,signals_submitted,fills,"
              << "blocks_cap,blocks_backpressure,blocks_cooldown,"
              << "submit_rate_pct,fill_rate_pct,"
              << "latency_min_us,latency_avg_us,latency_max_us\n";
        }
        
        uint64_t duration_ns = end_time_ns - start_time_ns;
        double duration_s = duration_ns / 1e9;
        
        uint64_t sig_gen = signals_generated.load(std::memory_order_relaxed);
        uint64_t sig_sub = signals_submitted.load(std::memory_order_relaxed);
        uint64_t f_val = fills.load(std::memory_order_relaxed);
        uint64_t b_cap = blocks_position_cap.load(std::memory_order_relaxed);
        uint64_t b_bp = blocks_backpressure.load(std::memory_order_relaxed);
        uint64_t b_cd = blocks_cooldown.load(std::memory_order_relaxed);
        
        uint64_t lat_sum = latency_sum_ns.load(std::memory_order_relaxed);
        uint64_t lat_cnt = latency_count.load(std::memory_order_relaxed);
        uint64_t lat_min = latency_min_ns.load(std::memory_order_relaxed);
        uint64_t lat_max = latency_max_ns.load(std::memory_order_relaxed);
        
        double submit_rate = 100.0 * sig_sub / std::max(1UL, sig_gen);
        double fill_rate = 100.0 * f_val / std::max(1UL, sig_sub);
        
        double avg_us = (lat_cnt > 0) ? (lat_sum / lat_cnt) / 1000.0 : 0.0;
        double min_us = (lat_min != UINT64_MAX) ? lat_min / 1000.0 : 0.0;
        double max_us = lat_max / 1000.0;
        
        f << tier_name << ","
          << duration_s << ","
          << sig_gen << ","
          << sig_sub << ","
          << f_val << ","
          << b_cap << ","
          << b_bp << ","
          << b_cd << ","
          << std::fixed << std::setprecision(2) << submit_rate << ","
          << fill_rate << ","
          << min_us << ","
          << avg_us << ","
          << max_us << "\n";
        
        f.close();
        std::cout << "Results saved to " << filename << "\n";
    }
    
private:
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
};

// Global benchmark instance
extern BenchmarkMetrics g_benchmark;

} // namespace chimera
