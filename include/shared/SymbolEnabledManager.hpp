// ═══════════════════════════════════════════════════════════════════════════════
// include/shared/SymbolEnabledManager.hpp - v4.3.2
// ═══════════════════════════════════════════════════════════════════════════════
// PURPOSE: Global atomic flags for symbol trading enabled/disabled
//          GUI sets these, SymbolThread checks before trading
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstring>
#include <iostream>

namespace Chimera {

// Maximum symbols we support
constexpr size_t MAX_SYMBOLS = 32;

class SymbolEnabledManager {
public:
    static SymbolEnabledManager& instance() {
        static SymbolEnabledManager inst;
        return inst;
    }
    
    // Set symbol enabled/disabled (called by GUI)
    void setEnabled(const char* symbol, bool enabled) {
        int idx = symbolToIndex(symbol);
        if (idx >= 0 && idx < static_cast<int>(MAX_SYMBOLS)) {
            enabled_[idx].store(enabled, std::memory_order_release);
            std::cout << "[SYMBOL-MGR] " << symbol << " -> " << (enabled ? "ENABLED" : "DISABLED") << "\n";
        }
    }
    
    // Check if symbol is enabled (called by SymbolThread on every tick)
    [[nodiscard]] bool isEnabled(const char* symbol) const noexcept {
        int idx = symbolToIndex(symbol);
        if (idx >= 0 && idx < static_cast<int>(MAX_SYMBOLS)) {
            return enabled_[idx].load(std::memory_order_acquire);
        }
        return false;  // Unknown symbol = disabled
    }
    
    // Disable all symbols (called before setting active set)
    void disableAll() {
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            enabled_[i].store(false, std::memory_order_release);
        }
        std::cout << "[SYMBOL-MGR] All symbols DISABLED\n";
    }
    
    // Enable specific symbols from comma-separated list
    void enableSymbols(const char* symbols) {
        // Parse comma-separated list
        std::string s(symbols);
        size_t pos = 0;
        while ((pos = s.find(',')) != std::string::npos || !s.empty()) {
            std::string sym;
            if (pos != std::string::npos) {
                sym = s.substr(0, pos);
                s.erase(0, pos + 1);
            } else {
                sym = s;
                s.clear();
            }
            // Trim whitespace
            while (!sym.empty() && (sym[0] == ' ' || sym[0] == '"')) sym.erase(0, 1);
            while (!sym.empty() && (sym.back() == ' ' || sym.back() == '"')) sym.pop_back();
            if (!sym.empty()) {
                setEnabled(sym.c_str(), true);
            }
        }
    }
    
private:
    SymbolEnabledManager() {
        // Start with ALL symbols DISABLED (v4.3.2 safety)
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            enabled_[i].store(false, std::memory_order_relaxed);
        }
    }
    
    // Convert symbol name to index (simple hash)
    static int symbolToIndex(const char* symbol) noexcept {
        if (!symbol) return -1;
        
        // Known symbols with fixed indices
        if (strcmp(symbol, "BTCUSDT") == 0) return 0;
        if (strcmp(symbol, "ETHUSDT") == 0) return 1;
        if (strcmp(symbol, "SOLUSDT") == 0) return 2;
        if (strcmp(symbol, "AVAXUSDT") == 0) return 3;
        if (strcmp(symbol, "LINKUSDT") == 0) return 4;
        if (strcmp(symbol, "ARBUSDT") == 0) return 5;
        if (strcmp(symbol, "OPUSDT") == 0) return 6;
        
        // CFD symbols
        if (strcmp(symbol, "XAUUSD") == 0) return 10;
        if (strcmp(symbol, "XAGUSD") == 0) return 11;
        if (strcmp(symbol, "NAS100") == 0) return 12;
        if (strcmp(symbol, "SPX500") == 0) return 13;
        if (strcmp(symbol, "US30") == 0) return 14;
        if (strcmp(symbol, "GER40") == 0) return 15;
        if (strcmp(symbol, "UK100") == 0) return 16;
        
        // FX symbols
        if (strcmp(symbol, "EURUSD") == 0) return 20;
        if (strcmp(symbol, "GBPUSD") == 0) return 21;
        if (strcmp(symbol, "USDJPY") == 0) return 22;
        if (strcmp(symbol, "AUDUSD") == 0) return 23;
        if (strcmp(symbol, "USDCAD") == 0) return 24;
        if (strcmp(symbol, "NZDUSD") == 0) return 25;
        if (strcmp(symbol, "USDCHF") == 0) return 26;
        if (strcmp(symbol, "AUDNZD") == 0) return 27;
        if (strcmp(symbol, "EURGBP") == 0) return 28;
        
        return -1;  // Unknown symbol
    }
    
    alignas(64) std::atomic<bool> enabled_[MAX_SYMBOLS];
};

// Convenience functions
inline bool isSymbolTradingEnabled(const char* symbol) {
    return SymbolEnabledManager::instance().isEnabled(symbol);
}

inline void setSymbolTradingEnabled(const char* symbol, bool enabled) {
    SymbolEnabledManager::instance().setEnabled(symbol, enabled);
}

inline void disableAllSymbolTrading() {
    SymbolEnabledManager::instance().disableAll();
}

}  // namespace Chimera
