#pragma once
#include <vector>
#include <string>
#include "ReplayEvent.hpp"

namespace chimera {

class ReplayLog {
public:
    void append(const ReplayEvent& ev);
    void save(const std::string& path) const;
    void load(const std::string& path);

    const std::vector<ReplayEvent>& events() const;

private:
    std::vector<ReplayEvent> m_events;
};

}
