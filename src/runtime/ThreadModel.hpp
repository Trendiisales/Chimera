#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include "runtime/CpuPinning.hpp"

namespace chimera {

class ThreadModel {
public:
    using ThreadFn = std::function<void()>;

    ThreadModel(int core_id, ThreadFn fn)
        : core_id_(core_id), fn_(fn) {}

    void start() {
        running_.store(true);
        thread_ = std::thread([this]() {
            CpuPinning::pin_thread(core_id_);
            fn_();
        });
    }

    void stop() { running_.store(false); }

    void join() {
        if (thread_.joinable()) thread_.join();
    }

    bool running() const { return running_.load(); }

private:
    int core_id_;
    ThreadFn fn_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}
