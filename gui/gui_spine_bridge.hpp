#pragma once
#include "core/event_spine.hpp"
#include "gui/spine_feed.hpp"

class GuiSpineBridge {
public:
    GuiSpineBridge(EventSpine& spine,
                   SpineFeed& gui);

    void pump();

private:
    EventSpine& spine_ref;
    SpineFeed& gui_ref;
    size_t last_idx;
};
