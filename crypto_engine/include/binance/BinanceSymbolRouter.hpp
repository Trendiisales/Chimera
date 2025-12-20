#pragma once
#include "BinanceSymbolContext.hpp"
#include "BinanceTypes.hpp"
#include <unordered_map>
#include <memory>

namespace binance {

/*
 Routes deltas to per-symbol contexts.
*/
class SymbolRouter {
    std::unordered_map<std::string, std::unique_ptr<SymbolContext>> symbols;

public:
    SymbolContext& get_or_create(const std::string& sym) {
        auto it = symbols.find(sym);
        if (it != symbols.end())
            return *it->second;

        auto ctx = std::make_unique<SymbolContext>(sym);
        auto& ref = *ctx;
        symbols.emplace(sym, std::move(ctx));
        return ref;
    }

    template<typename Fn>
    void for_each(Fn&& fn) {
        for (auto& kv : symbols)
            fn(*kv.second);
    }
};

}
