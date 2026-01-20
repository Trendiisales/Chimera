#pragma once
#include <string>
#include <mutex>

static std::string GUI_LIVE_DATA;
static std::mutex GUI_LIVE_MUTEX;

inline void gui_set_html(const std::string& s) {
    std::lock_guard<std::mutex> lock(GUI_LIVE_MUTEX);
    GUI_LIVE_DATA = s;
}

inline std::string gui_get_html() {
    std::lock_guard<std::mutex> lock(GUI_LIVE_MUTEX);
    return GUI_LIVE_DATA;
}
