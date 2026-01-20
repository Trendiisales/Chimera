#include "gui_snapshot_bus.hpp"
#include <mutex>

static std::mutex g_lock;

GuiSnapshotBus::GuiSnapshotBus() : version_(0), buffer_("BOOTING\n") {}

GuiSnapshotBus& GuiSnapshotBus::instance() {
    static GuiSnapshotBus bus;
    return bus;
}

void GuiSnapshotBus::update(const std::string& snapshot) {
    std::lock_guard<std::mutex> lock(g_lock);
    buffer_ = snapshot;
    version_.fetch_add(1);
}

std::string GuiSnapshotBus::get() const {
    std::lock_guard<std::mutex> lock(g_lock);
    return buffer_;
}
