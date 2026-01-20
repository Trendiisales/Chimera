#pragma once
#include <string>
#include <mutex>

class GuiSnapshotBus {
public:
    static GuiSnapshotBus& instance() {
        static GuiSnapshotBus bus;
        return bus;
    }

    void update(const std::string& json) {
        std::lock_guard<std::mutex> lock(mu_);
        snapshot_ = json;
    }

    std::string get() {
        std::lock_guard<std::mutex> lock(mu_);
        return snapshot_;
    }

private:
    GuiSnapshotBus() = default;
    std::mutex mu_;
    std::string snapshot_;
};
