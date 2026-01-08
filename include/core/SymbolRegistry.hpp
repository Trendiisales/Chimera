// =============================================================================
// SymbolRegistry.hpp - Authoritative Symbol Routing (Single Source of Truth)
// =============================================================================
// PURPOSE: Eliminates silent routing failures by centralizing all symbol checks
// 
// v4.12.0: CRYPTO REMOVED - CFD only
//
// GUARANTEES:
//   - Forex-only gates cannot starve metals
//   - Alias mismatch (GOLD vs XAUUSD) handled automatically
//   - GUI vs engine drift impossible
//   - assertKnown() crashes loudly on unknown symbols
// =============================================================================
#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdio>

namespace Chimera {

class SymbolRegistry {
public:
    // =========================================================================
    // REGISTRATION (startup only)
    // =========================================================================
    void registerForex(const std::vector<std::string>& syms) {
        for (const auto& s : syms) {
            forex_.insert(normalize(s));
        }
    }

    void registerMetals(const std::vector<std::string>& syms) {
        for (const auto& s : syms) {
            metals_.insert(normalize(s));
        }
    }

    void registerIndices(const std::vector<std::string>& syms) {
        for (const auto& s : syms) {
            indices_.insert(normalize(s));
        }
    }

    // =========================================================================
    // QUERIES (runtime hot path - must be fast)
    // =========================================================================
    bool isForex(const std::string& s) const {
        return forex_.count(normalize(s)) > 0;
    }

    bool isMetal(const std::string& s) const {
        return metals_.count(normalize(s)) > 0;
    }

    bool isIndex(const std::string& s) const {
        return indices_.count(normalize(s)) > 0;
    }

    // CFD = Forex + Metals + Indices (anything via FIX/cTrader)
    bool isCfd(const std::string& s) const {
        std::string n = normalize(s);
        return forex_.count(n) > 0 || metals_.count(n) > 0 || indices_.count(n) > 0;
    }

    // Any known tradeable symbol (v4.12.0: CFD only)
    bool isKnown(const std::string& s) const {
        return isCfd(s);
    }

    // =========================================================================
    // SAFETY ASSERTIONS (crash loudly on bugs)
    // =========================================================================
    void assertKnown(const std::string& s) const {
        if (!isKnown(s)) {
            fprintf(stderr, "[SYMBOL-REGISTRY] FATAL: Unknown symbol '%s'\n", s.c_str());
            throw std::runtime_error("SYMBOL NOT REGISTERED: " + s);
        }
    }

    void assertCfd(const std::string& s) const {
        if (!isCfd(s)) {
            fprintf(stderr, "[SYMBOL-REGISTRY] FATAL: '%s' is not a CFD symbol\n", s.c_str());
            throw std::runtime_error("NOT A CFD SYMBOL: " + s);
        }
    }

    // =========================================================================
    // INTROSPECTION (audit/logging)
    // =========================================================================
    std::vector<std::string> allCfd() const {
        std::vector<std::string> out;
        out.insert(out.end(), forex_.begin(), forex_.end());
        out.insert(out.end(), metals_.begin(), metals_.end());
        out.insert(out.end(), indices_.begin(), indices_.end());
        return out;
    }

    std::vector<std::string> allForex() const {
        return std::vector<std::string>(forex_.begin(), forex_.end());
    }

    std::vector<std::string> allMetals() const {
        return std::vector<std::string>(metals_.begin(), metals_.end());
    }

    std::vector<std::string> allIndices() const {
        return std::vector<std::string>(indices_.begin(), indices_.end());
    }

    size_t forexCount() const { return forex_.size(); }
    size_t metalsCount() const { return metals_.size(); }
    size_t indicesCount() const { return indices_.size(); }
    size_t totalCount() const { return forex_.size() + metals_.size() + indices_.size(); }

    // =========================================================================
    // DEBUG: Print all registered symbols
    // =========================================================================
    void dump() const {
        printf("[SYMBOL-REGISTRY] Forex (%zu):", forex_.size());
        for (const auto& s : forex_) printf(" %s", s.c_str());
        printf("\n");
        
        printf("[SYMBOL-REGISTRY] Metals (%zu):", metals_.size());
        for (const auto& s : metals_) printf(" %s", s.c_str());
        printf("\n");
        
        printf("[SYMBOL-REGISTRY] Indices (%zu):", indices_.size());
        for (const auto& s : indices_) printf(" %s", s.c_str());
        printf("\n");
    }

private:
    // =========================================================================
    // NORMALIZATION: Handle aliases and case
    // =========================================================================
    static std::string normalize(std::string s) {
        // Remove slashes (XAU/USD -> XAUUSD)
        s.erase(std::remove(s.begin(), s.end(), '/'), s.end());
        
        // Uppercase
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        
        // Alias normalization
        if (s == "GOLD") return "XAUUSD";
        if (s == "SILVER") return "XAGUSD";
        if (s == "DOW" || s == "DOW30" || s == "DJIA") return "US30";
        if (s == "NASDAQ" || s == "NDX") return "NAS100";
        if (s == "SP500" || s == "SNP500") return "SPX500";
        
        return s;
    }

    std::unordered_set<std::string> forex_;
    std::unordered_set<std::string> metals_;
    std::unordered_set<std::string> indices_;
};

// Global singleton
inline SymbolRegistry& getSymbolRegistry() {
    static SymbolRegistry instance;
    return instance;
}

} // namespace Chimera
