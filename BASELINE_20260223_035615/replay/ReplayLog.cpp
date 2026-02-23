#include "ReplayLog.hpp"
#include <fstream>

namespace chimera {

void ReplayLog::append(const ReplayEvent& ev) {
    m_events.push_back(ev);
}

void ReplayLog::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    for (const ReplayEvent& e : m_events) {
        uint8_t type = static_cast<uint8_t>(e.type);
        uint64_t len = e.payload_json.size();

        out.write((char*)&type, sizeof(type));
        out.write((char*)&e.ts_ns, sizeof(e.ts_ns));
        out.write((char*)&e.causal_id, sizeof(e.causal_id));
        out.write((char*)&len, sizeof(len));
        out.write(e.payload_json.data(), len);
    }
}

void ReplayLog::load(const std::string& path) {
    m_events.clear();
    std::ifstream in(path, std::ios::binary);

    while (in.good()) {
        ReplayEvent e{};
        uint8_t type;
        uint64_t len;

        if (!in.read((char*)&type, sizeof(type))) break;
        in.read((char*)&e.ts_ns, sizeof(e.ts_ns));
        in.read((char*)&e.causal_id, sizeof(e.causal_id));
        in.read((char*)&len, sizeof(len));

        e.type = static_cast<ReplayEventType>(type);
        e.payload_json.resize(len);
        in.read(&e.payload_json[0], len);

        m_events.push_back(e);
    }
}

const std::vector<ReplayEvent>& ReplayLog::events() const {
    return m_events;
}

}
