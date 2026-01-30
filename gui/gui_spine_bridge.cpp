#include "gui/gui_spine_bridge.hpp"

GuiSpineBridge::GuiSpineBridge(EventSpine& s,
                               SpineFeed& g)
    : spine_ref(s), gui_ref(g), last_idx(0) {}

void GuiSpineBridge::pump() {
    auto snap = spine_ref.snapshot();
    for (size_t i = last_idx; i < snap.size(); ++i) {
        const auto& e = snap[i];
        gui_ref.publish("SPINE",
                        std::to_string(e.id) + "|" +
                        std::to_string(static_cast<int>(e.type)) + "|" +
                        e.payload);
    }
    last_idx = snap.size();
}
