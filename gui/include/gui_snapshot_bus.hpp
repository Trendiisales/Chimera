#pragma once
#include <string>

class GuiSnapshotBus {
public:
    static GuiSnapshotBus& instance();
    std::string get();

private:
    GuiSnapshotBus() = default;
};
