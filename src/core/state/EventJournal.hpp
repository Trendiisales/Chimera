#pragma once
#include <fstream>
#include <string>
#include <atomic>
#include <cstdint>

namespace chimera {

class EventJournal {
public:
    explicit EventJournal(const std::string& path);
    ~EventJournal();

    uint64_t nextEventId();
    void write(const std::string& type,
               const std::string& payload,
               uint64_t causal_id = 0);

private:
    std::ofstream m_bin;
    std::ofstream m_json;
    std::atomic<uint64_t> m_event_id;
};

}
