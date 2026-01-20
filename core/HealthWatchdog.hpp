#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <functional>
#include <iostream>
#include <deque>
#include <mutex>
#include <cstdint>
#include <string>

class HealthWatchdog {
public:
    explicit HealthWatchdog(std::function<void(const std::string&)> flatten_callback)
        : flatten_callback_(flatten_callback),
          running_(false)
    {}

    ~HealthWatchdog() {
        stop();
    }

    void start() {
        running_.store(true, std::memory_order_release);
        arm();

        watchdog_thread_ = std::thread([this]() {
            std::cout << "[WATCHDOG] Started - monitoring system health" << std::endl;

            while (running_.load(std::memory_order_acquire)) {
                checkHealth();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::cout << "[WATCHDOG] Stopped" << std::endl;
        });
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        if (watchdog_thread_.joinable()) {
            watchdog_thread_.join();
        }
    }

    void onTick() {
        last_tick_ms_.store(nowMs(), std::memory_order_release);
        armed_.store(true, std::memory_order_release);
    }

    void onFill() {
        last_fill_ms_.store(nowMs(), std::memory_order_release);
    }

    void onPositionOpen() {
        positions_open_.fetch_add(1, std::memory_order_relaxed);
    }

    void onPositionClose() {
        positions_open_.fetch_sub(1, std::memory_order_relaxed);
    }

    void updatePnL(double pnl_bps) {
        total_pnl_bps_.store(pnl_bps, std::memory_order_relaxed);
    }

    void onWSReconnect() {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        int64_t now = nowMs();
        int64_t window_start = now - static_cast<int64_t>(RECONNECT_WINDOW_MS);

        while (!reconnect_times_.empty() &&
               reconnect_times_.front() < window_start) {
            reconnect_times_.pop_front();
        }

        reconnect_times_.push_back(now);
    }

    void onDepthCorruption() {
        depth_corruption_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void onDepthResync() {
        depth_corruption_count_.store(0, std::memory_order_relaxed);
    }

    void arm() {
        int64_t now = nowMs();
        last_tick_ms_.store(now, std::memory_order_release);
        last_fill_ms_.store(now, std::memory_order_release);
        flatten_triggered_.store(false, std::memory_order_release);
        armed_.store(true, std::memory_order_release);
    }

private:
    using Clock = std::chrono::steady_clock;

    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()
        ).count();
    }

    void checkHealth() {
        if (!armed_.load(std::memory_order_acquire)) {
            return;
        }

        int64_t now = nowMs();
        int64_t last_tick = last_tick_ms_.load(std::memory_order_acquire);

        if (last_tick <= 0 || now < last_tick) {
            return;
        }

        int64_t tick_age = now - last_tick;

        if (tick_age > static_cast<int64_t>(TICK_TIMEOUT_MS)) {
            triggerFlatten("TICK_TIMEOUT " + std::to_string(tick_age) + "ms");
            return;
        }

        if (positions_open_.load(std::memory_order_relaxed) > 0) {
            int64_t last_fill = last_fill_ms_.load(std::memory_order_acquire);
            if (last_fill > 0 && now >= last_fill) {
                int64_t fill_age = now - last_fill;
                if (fill_age > static_cast<int64_t>(FILL_TIMEOUT_MS)) {
                    triggerFlatten("FILL_TIMEOUT " + std::to_string(fill_age) + "ms");
                    return;
                }
            }
        }

        double pnl = total_pnl_bps_.load(std::memory_order_relaxed);
        if (pnl < MAX_DAILY_DD_BPS) {
            triggerFlatten("DRAWDOWN_LIMIT");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(reconnect_mutex_);
            if (reconnect_times_.size() >= MAX_RECONNECTS_IN_WINDOW) {
                triggerFlatten("WS_INSTABILITY");
                return;
            }
        }

        uint64_t corruption_count = depth_corruption_count_.load(std::memory_order_relaxed);
        if (corruption_count >= MAX_DEPTH_CORRUPTIONS) {
            triggerFlatten("DEPTH_CORRUPTION");
            return;
        }
    }

    void triggerFlatten(const std::string& reason) {
        if (flatten_triggered_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        std::cerr << "[WATCHDOG] FLATTEN TRIGGERED: " << reason << std::endl;

        if (flatten_callback_) {
            flatten_callback_(reason);
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
        flatten_triggered_.store(false, std::memory_order_release);
    }

private:
    static constexpr uint64_t TICK_TIMEOUT_MS = 500;
    static constexpr uint64_t FILL_TIMEOUT_MS = 5000;
    static constexpr double   MAX_DAILY_DD_BPS = -25.0;
    static constexpr uint64_t RECONNECT_WINDOW_MS = 10000;
    static constexpr size_t   MAX_RECONNECTS_IN_WINDOW = 3;
    static constexpr uint64_t MAX_DEPTH_CORRUPTIONS = 5;

    std::function<void(const std::string&)> flatten_callback_;

    std::thread watchdog_thread_;
    std::atomic<bool> running_;

    std::atomic<int64_t> last_tick_ms_{-1};
    std::atomic<int64_t> last_fill_ms_{-1};
    std::atomic<int> positions_open_{0};
    std::atomic<double> total_pnl_bps_{0.0};
    std::atomic<bool> flatten_triggered_{false};
    std::atomic<bool> armed_{false};
    std::atomic<uint64_t> depth_corruption_count_{0};

    std::mutex reconnect_mutex_;
    std::deque<int64_t> reconnect_times_;
};
