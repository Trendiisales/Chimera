#pragma once
#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

struct TelemetryEvent {
    std::string type;
    uint64_t ts;
    std::map<std::string, std::string> fields;
};

class TelemetryBus {
    void publish(const std::string& type, const std::map<std::string, double>& fields) {\

        std::map<std::string, std::string> out;\

        for (const auto& kv : fields) {\

            out[kv.first] = std::to_string(kv.second);\

        }\

        push(type, out);\

    }\


public:
    static TelemetryBus& instance() {
        static TelemetryBus b;
        return b;
    }

    void push(const std::string& type,
              const std::map<std::string, std::string>& fields) {
        TelemetryEvent e;
        e.type = type;
        e.ts = now();
        e.fields = fields;

        {
            std::lock_guard<std::mutex> lk(q_mtx);
            queue.push_back(e);
        }
        cv.notify_one();
    }

    std::deque<TelemetryEvent> snapshot() {
        std::lock_guard<std::mutex> lk(buf_mtx);
        return buffer;
    }

    void start() {
        if (running) return;
        running = true;
        worker = std::thread([this]() { pump(); });
        worker.detach();
    }

private:
    TelemetryBus() : running(false) {}

    void pump() {
        while (running) {
            TelemetryEvent e;
            {
                std::unique_lock<std::mutex> lk(q_mtx);
                cv.wait(lk, [&]() { return !queue.empty(); });
                e = queue.front();
                queue.pop_front();
            }

            {
                std::lock_guard<std::mutex> lk(buf_mtx);
                buffer.push_back(e);
                if (buffer.size() > 100) buffer.pop_front();
            }
        }
    }

    uint64_t now() {
        return (uint64_t)time(nullptr);
    }

    std::deque<TelemetryEvent> queue;
    std::deque<TelemetryEvent> buffer;

    std::mutex q_mtx;
    std::mutex buf_mtx;
    std::condition_variable cv;

    std::thread worker;
    std::atomic<bool> running;
};
