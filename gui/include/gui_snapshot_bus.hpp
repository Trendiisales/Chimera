#pragma once
#include <atomic>
#include <string>

class GuiSnapshotBus {
public:
    static GuiSnapshotBus& instance();

    void update(const std::string& snapshot);
    std::string get() const;

private:
    GuiSnapshotBus();
    std::atomic<unsigned long long> version_;
    std::string buffer_;
};
