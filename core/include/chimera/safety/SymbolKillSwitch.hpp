#pragma once
#include <unordered_map>
#include <string>
#include <atomic>

namespace chimera::safety {

class SymbolKillSwitch {
public:
    void trigger(const std::string& sym) {
        flags[sym].store(true);
    }

    bool active(const std::string& sym) const {
        auto it = flags.find(sym);
        if (it == flags.end()) return false;
        return it->second.load();
    }

    void clear(const std::string& sym) {
        flags[sym].store(false);
    }

private:
    std::unordered_map<std::string, std::atomic<bool>> flags;
};

}
