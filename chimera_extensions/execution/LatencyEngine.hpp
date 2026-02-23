#pragma once

#include <unordered_map>
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../core/ThreadSafeQueue.hpp"

namespace chimera {
namespace execution {

struct ExecutionStats {
    std::string order_id;
    double send_to_ack_ms = 0.0;
    double ack_to_fill_ms = 0.0;
    double total_latency_ms = 0.0;
    double slippage = 0.0;
    double spread_at_send = 0.0;
    double quality_score = 0.0;
};

struct OrderLifecycle {
    std::chrono::high_resolution_clock::time_point send_time;
    std::chrono::high_resolution_clock::time_point ack_time;
    double send_mid = 0.0;
    double spread_at_send = 0.0;
    bool acknowledged = false;
};

class LatencyEngine {
public:
    explicit LatencyEngine(core::ThreadSafeQueue<ExecutionStats>& telemetry_queue);

    void on_order_sent(const std::string& order_id, double mid_price, double spread);
    void on_ack(const std::string& order_id);
    void on_fill(const std::string& order_id, double fill_price);

    double get_latency_ema() const;
    double get_slippage_ema() const;

private:
    double compute_quality_score(double slippage, double latency_ms, double spread);
    void update_ema(std::atomic<double>& ema, double value, double alpha);

    std::unordered_map<std::string, OrderLifecycle> m_active_orders;
    mutable std::mutex m_mutex;

    core::ThreadSafeQueue<ExecutionStats>& m_telemetry_queue;

    std::atomic<double> m_latency_ema{0.0};
    std::atomic<double> m_slippage_ema{0.0};
};

} // namespace execution
} // namespace chimera
