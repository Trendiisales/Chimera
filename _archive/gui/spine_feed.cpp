#include "gui/spine_feed.hpp"
#include <chrono>

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SpineFeed::publish(const std::string& type, const std::string& payload) {
    std::lock_guard<std::mutex> g(lock);
    events.push_back({ now_ns(), type, payload });

    if (events.size() > 512)
        events.erase(events.begin());
}

std::vector<GuiEvent> SpineFeed::snapshot() {
    std::lock_guard<std::mutex> g(lock);
    return events;
}
