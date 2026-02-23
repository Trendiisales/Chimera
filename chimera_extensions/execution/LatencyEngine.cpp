#include "LatencyEngine.hpp"
#include <cmath>
#include <algorithm>

namespace chimera {
namespace execution {

LatencyEngine::LatencyEngine(core::ThreadSafeQueue<ExecutionStats>& telemetry_queue)
    : m_telemetry_queue(telemetry_queue) {}

void LatencyEngine::on_order_sent(const std::string& order_id, double mid_price, double spread) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    OrderLifecycle lifecycle;
    lifecycle.send_time = std::chrono::high_resolution_clock::now();
    lifecycle.send_mid = mid_price;
    lifecycle.spread_at_send = spread;
    
    m_active_orders[order_id] = lifecycle;
}

void LatencyEngine::on_ack(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_active_orders.find(order_id);
    if (it == m_active_orders.end())
        return;
    
    it->second.ack_time = std::chrono::high_resolution_clock::now();
    it->second.acknowledged = true;
}

void LatencyEngine::on_fill(const std::string& order_id, double fill_price) {
    ExecutionStats stats;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_active_orders.find(order_id);
        if (it == m_active_orders.end())
            return;
        
        auto now = std::chrono::high_resolution_clock::now();
        
        double send_to_ack = 0.0;
        double ack_to_fill = 0.0;
        
        if (it->second.acknowledged) {
            send_to_ack = std::chrono::duration<double, std::milli>(
                it->second.ack_time - it->second.send_time).count();
            ack_to_fill = std::chrono::duration<double, std::milli>(
                now - it->second.ack_time).count();
        }
        
        double total_latency = std::chrono::duration<double, std::milli>(
            now - it->second.send_time).count();
        
        double slippage = fill_price - it->second.send_mid;
        
        stats.order_id = order_id;
        stats.send_to_ack_ms = send_to_ack;
        stats.ack_to_fill_ms = ack_to_fill;
        stats.total_latency_ms = total_latency;
        stats.slippage = slippage;
        stats.spread_at_send = it->second.spread_at_send;
        stats.quality_score = compute_quality_score(slippage, total_latency, it->second.spread_at_send);
        
        m_active_orders.erase(it);
    }
    
    update_ema(m_latency_ema, stats.total_latency_ms, 0.1);
    update_ema(m_slippage_ema, std::abs(stats.slippage), 0.1);
    
    m_telemetry_queue.push(stats);
}

double LatencyEngine::compute_quality_score(double slippage, double latency_ms, double spread) {
    double spread_factor = (spread > 0.0) ? std::abs(slippage) / spread : 1.0;
    double latency_factor = latency_ms / 50.0;
    
    double score = 1.0 - (spread_factor * 0.5) - (latency_factor * 0.3);
    return std::clamp(score, 0.0, 1.0);
}

void LatencyEngine::update_ema(std::atomic<double>& ema, double value, double alpha) {
    double current = ema.load();
    double updated = alpha * value + (1.0 - alpha) * current;
    ema.store(updated);
}

double LatencyEngine::get_latency_ema() const {
    return m_latency_ema.load();
}

double LatencyEngine::get_slippage_ema() const {
    return m_slippage_ema.load();
}

} // namespace execution
} // namespace chimera
