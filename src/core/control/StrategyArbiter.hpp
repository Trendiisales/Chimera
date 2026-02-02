#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include "state/EventJournal.hpp"

namespace chimera {

class StrategyArbiter {
public:
    explicit StrategyArbiter(EventJournal& journal);
    bool allow(const std::string& engine,
               const std::string& symbol);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, uint64_t> m_last_trade;
};

}
