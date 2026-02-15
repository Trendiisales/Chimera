#pragma once
#include <vector>
#include "SymbolState.hpp"

namespace ChimeraV2 {

class SymbolRegistry {
public:
    SymbolRegistry() {
        symbols_.push_back({"XAUUSD"});
        symbols_.push_back({"XAGUSD"});
    }

    std::vector<SymbolState>& symbols() { return symbols_; }
    const std::vector<SymbolState>& symbols() const { return symbols_; }

    SymbolState* find(const std::string& symbol) {
        for (auto& s : symbols_) {
            if (s.symbol == symbol) return &s;
        }
        return nullptr;
    }

private:
    std::vector<SymbolState> symbols_;
};

}
