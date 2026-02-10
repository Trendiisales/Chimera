#pragma once
// =============================================================================
// TradingConfig.hpp - Live Trading Configuration with Per-Asset-Class Settings
// =============================================================================
// Based on quant fund risk parameters:
// - Position sizing: 0.5-2% risk per trade
// - Daily drawdown: 2-5%
// - Max drawdown: 6-12%
// - VPIN threshold: 0.5-0.7 (toxic flow cutoff)
// - Spread threshold: varies by asset class
// =============================================================================

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace Chimera {

// =============================================================================
// Risk Level Presets
// =============================================================================
enum class RiskLevel : int {
    CONSERVATIVE = 0,  // Tight stops, small size, strict filters
    BALANCED = 1,      // Standard institutional parameters
    AGGRESSIVE = 2     // Wider stops, larger size, looser filters
};

// =============================================================================
// Per-Symbol Trading Config
// =============================================================================
struct SymbolConfig {
    char symbol[16] = {0};
    bool enabled = true;
    
    // Position sizing
    double position_size = 0.001;     // Base lot size
    double max_position = 0.01;       // Max position per symbol
    double risk_per_trade_pct = 1.0;  // % of account to risk
    
    // Entry/Exit
    double stop_loss_bps = 25;        // Stop loss in basis points
    double take_profit_bps = 45;      // Take profit in basis points
    double min_spread_bps = 1.0;      // Don't trade if spread > this
    double max_spread_bps = 10.0;     // Hard cutoff
    
    // Microstructure filters
    double vpin_threshold = 0.60;     // Skip if VPIN > this (toxic flow)
    double ofi_threshold = 0.55;      // Order flow imbalance threshold
    double min_depth = 10000;         // Minimum book depth
    
    // Timing
    int cooldown_ms = 250;            // Min ms between trades
    int max_latency_us = 600;         // Skip if latency > this
    
    // Session filter (UTC hours)
    int session_start_utc = 8;        // London open
    int session_end_utc = 20;         // NY close
    
    void setSymbol(const char* s) {
        strncpy(symbol, s, 15);
        symbol[15] = '\0';
    }
};

// =============================================================================
// Asset Class Config (groups of symbols)
// =============================================================================
struct AssetClassConfig {
    char name[16] = {0};
    int asset_class = 0;  // 0=reserved, 1=forex, 2=metals, 3=indices (v4.11.0: crypto removed)
    
    // Default values for this asset class
    double default_size = 0.001;
    double default_max_pos = 0.01;
    double default_sl_bps = 25;
    double default_tp_bps = 45;
    double default_max_spread_bps = 10;
    double default_vpin = 0.60;
    double default_ofi = 0.55;
    int default_cooldown_ms = 250;
    
    void setName(const char* n) {
        strncpy(name, n, 15);
        name[15] = '\0';
    }
};

// =============================================================================
// Global Trading Config
// =============================================================================
class TradingConfig {
public:
    static constexpr int MAX_SYMBOLS = 20;
    static constexpr int NUM_ASSET_CLASSES = 4;
    
    TradingConfig() {
        initDefaults();
    }
    
    // =========================================================================
    // Preset Loading
    // =========================================================================
    void loadPreset(RiskLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        risk_level_ = level;
        
        switch (level) {
            case RiskLevel::CONSERVATIVE:
                loadConservative();
                break;
            case RiskLevel::BALANCED:
                loadBalanced();
                break;
            case RiskLevel::AGGRESSIVE:
                loadAggressive();
                break;
        }
        
        // Apply asset class defaults to all symbols
        applyAssetClassDefaults();
    }
    
    // =========================================================================
    // Symbol Access
    // =========================================================================
    SymbolConfig* getSymbolConfig(const char* symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < symbol_count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) {
                return &symbols_[i];
            }
        }
        return nullptr;
    }
    
    const SymbolConfig* getSymbolConfig(const char* symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < symbol_count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) {
                return &symbols_[i];
            }
        }
        return nullptr;
    }
    
    // =========================================================================
    // Asset Class Access
    // =========================================================================
    AssetClassConfig* getAssetClassConfig(int asset_class) {
        if (asset_class < 0 || asset_class >= NUM_ASSET_CLASSES) return nullptr;
        return &asset_classes_[asset_class];
    }
    
    // =========================================================================
    // Global Settings
    // =========================================================================
    double getDailyLossLimit() const { return daily_loss_limit_; }
    double getMaxDrawdownPct() const { return max_drawdown_pct_; }
    double getMaxExposure() const { return max_exposure_; }
    int getMaxPositions() const { return max_positions_; }
    RiskLevel getRiskLevel() const { return risk_level_; }
    
    void setDailyLossLimit(double v) { daily_loss_limit_ = v; }
    void setMaxDrawdownPct(double v) { max_drawdown_pct_ = v; }
    void setMaxExposure(double v) { max_exposure_ = v; }
    void setMaxPositions(int v) { max_positions_ = v; }
    
    // =========================================================================
    // Update single symbol config (from GUI)
    // =========================================================================
    bool updateSymbolConfig(const char* symbol, const SymbolConfig& cfg) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < symbol_count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) {
                // Preserve symbol name
                char name[16];
                strncpy(name, symbols_[i].symbol, 15);
                symbols_[i] = cfg;
                strncpy(symbols_[i].symbol, name, 15);
                return true;
            }
        }
        return false;
    }
    
    // =========================================================================
    // Update asset class defaults (from GUI)
    // =========================================================================
    bool updateAssetClassConfig(int asset_class, const AssetClassConfig& cfg) {
        if (asset_class < 0 || asset_class >= NUM_ASSET_CLASSES) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        asset_classes_[asset_class] = cfg;
        return true;
    }
    
    // =========================================================================
    // JSON serialization for GUI
    // =========================================================================
    std::string toJSON() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string json = "{";
        
        // Global settings
        json += "\"risk_level\":" + std::to_string(static_cast<int>(risk_level_)) + ",";
        json += "\"daily_loss_limit\":" + std::to_string(daily_loss_limit_) + ",";
        json += "\"max_drawdown_pct\":" + std::to_string(max_drawdown_pct_) + ",";
        json += "\"max_exposure\":" + std::to_string(max_exposure_) + ",";
        json += "\"max_positions\":" + std::to_string(max_positions_) + ",";
        
        // Asset classes
        json += "\"asset_classes\":[";
        for (int i = 0; i < NUM_ASSET_CLASSES; i++) {
            if (i > 0) json += ",";
            json += assetClassToJSON(asset_classes_[i]);
        }
        json += "],";
        
        // Symbols
        json += "\"symbols\":[";
        for (int i = 0; i < symbol_count_; i++) {
            if (i > 0) json += ",";
            json += symbolToJSON(symbols_[i]);
        }
        json += "]";
        
        json += "}";
        return json;
    }
    
    // =========================================================================
    // Get symbol count
    // =========================================================================
    int getSymbolCount() const { return symbol_count_; }
    const SymbolConfig* getSymbols() const { return symbols_; }
    
    // v7.04: Get symbol by index for iteration
    SymbolConfig* getSymbolByIndex(int idx) {
        if (idx < 0 || idx >= symbol_count_) return nullptr;
        return &symbols_[idx];
    }

private:
    mutable std::mutex mutex_;
    
    // Global risk settings
    RiskLevel risk_level_ = RiskLevel::CONSERVATIVE;
    double daily_loss_limit_ = -200.0;   // NZD - HARDCODED HARD STOP
    double max_drawdown_pct_ = 10.0;     // 10% max DD
    double max_exposure_ = 0.05;         // 5% max exposure
    int max_positions_ = 3;
    
    // Asset class configs
    AssetClassConfig asset_classes_[NUM_ASSET_CLASSES];
    
    // Symbol configs
    SymbolConfig symbols_[MAX_SYMBOLS];
    int symbol_count_ = 0;
    
    // =========================================================================
    // Initialize default symbols and asset classes
    // v4.11.0: CRYPTO REMOVED - CFD only
    // =========================================================================
    void initDefaults() {
        // Asset class defaults
        // 0 = Reserved (was crypto - now unused)
        asset_classes_[0].setName("Reserved");
        asset_classes_[0].asset_class = 0;
        asset_classes_[0].default_size = 0.0;
        asset_classes_[0].default_max_pos = 0.0;
        asset_classes_[0].default_sl_bps = 0;
        asset_classes_[0].default_tp_bps = 0;
        asset_classes_[0].default_max_spread_bps = 0;
        asset_classes_[0].default_vpin = 0.0;
        asset_classes_[0].default_ofi = 0.0;
        asset_classes_[0].default_cooldown_ms = 0;
        
        // 1 = Forex
        asset_classes_[1].setName("Forex");
        asset_classes_[1].asset_class = 1;
        asset_classes_[1].default_size = 0.01;
        asset_classes_[1].default_max_pos = 0.1;
        asset_classes_[1].default_sl_bps = 15;
        asset_classes_[1].default_tp_bps = 30;
        asset_classes_[1].default_max_spread_bps = 2;
        asset_classes_[1].default_vpin = 0.65;
        asset_classes_[1].default_ofi = 0.50;
        asset_classes_[1].default_cooldown_ms = 500;
        
        // 2 = Metals
        asset_classes_[2].setName("Metals");
        asset_classes_[2].asset_class = 2;
        asset_classes_[2].default_size = 0.01;
        asset_classes_[2].default_max_pos = 0.05;
        asset_classes_[2].default_sl_bps = 20;
        asset_classes_[2].default_tp_bps = 40;
        asset_classes_[2].default_max_spread_bps = 3;
        asset_classes_[2].default_vpin = 0.60;
        asset_classes_[2].default_ofi = 0.55;
        asset_classes_[2].default_cooldown_ms = 300;
        
        // 3 = Indices
        asset_classes_[3].setName("Indices");
        asset_classes_[3].asset_class = 3;
        asset_classes_[3].default_size = 0.1;
        asset_classes_[3].default_max_pos = 1.0;
        asset_classes_[3].default_sl_bps = 10;
        asset_classes_[3].default_tp_bps = 20;
        asset_classes_[3].default_max_spread_bps = 2;
        asset_classes_[3].default_vpin = 0.70;
        asset_classes_[3].default_ofi = 0.45;
        asset_classes_[3].default_cooldown_ms = 200;
        
        // Initialize symbols - CFD only (crypto removed v4.11.0)
        addSymbol("EURUSD", 1, false);    
        addSymbol("GBPUSD", 1, false);    
        addSymbol("USDJPY", 1, false);    
        addSymbol("AUDUSD", 1, false);   
        addSymbol("USDCAD", 1, false);   
        addSymbol("AUDNZD", 1, false);   
        addSymbol("USDCHF", 1, false);   
        addSymbol("XAUUSD", 2, false);   
        addSymbol("XAGUSD", 2, false);   
        addSymbol("NAS100", 3, false);   
        addSymbol("SPX500", 3, false);   
        addSymbol("US30", 3, false);
        
        // Load conservative by default
        loadConservative();
        applyAssetClassDefaults();
    }
    
    void addSymbol(const char* name, int asset_class, bool enabled = false) {
        (void)asset_class;  // Reserved for future use
        if (symbol_count_ >= MAX_SYMBOLS) return;
        symbols_[symbol_count_].setSymbol(name);
        symbols_[symbol_count_].enabled = enabled;  // v6.99: Respect enabled parameter
        symbol_count_++;
    }
    
    // =========================================================================
    // CONSERVATIVE - Tight risk, small size, strict filters
    // =========================================================================
    void loadConservative() {
        daily_loss_limit_ = -300.0;
        max_drawdown_pct_ = 6.0;
        max_exposure_ = 0.02;
        max_positions_ = 2;
        
        // v4.11.0: Class 0 reserved (crypto removed)
        
        // Forex - conservative
        asset_classes_[1].default_size = 0.005;
        asset_classes_[1].default_sl_bps = 10;
        asset_classes_[1].default_tp_bps = 20;
        asset_classes_[1].default_max_spread_bps = 1.5;
        asset_classes_[1].default_vpin = 0.60;
        asset_classes_[1].default_cooldown_ms = 750;
        
        // Metals - conservative
        asset_classes_[2].default_size = 0.005;
        asset_classes_[2].default_sl_bps = 15;
        asset_classes_[2].default_tp_bps = 30;
        asset_classes_[2].default_max_spread_bps = 2;
        asset_classes_[2].default_vpin = 0.55;
        asset_classes_[2].default_cooldown_ms = 500;
        
        // Indices - conservative
        asset_classes_[3].default_size = 0.05;
        asset_classes_[3].default_sl_bps = 8;
        asset_classes_[3].default_tp_bps = 15;
        asset_classes_[3].default_max_spread_bps = 1.5;
        asset_classes_[3].default_vpin = 0.65;
        asset_classes_[3].default_cooldown_ms = 300;
    }
    
    // =========================================================================
    // BALANCED - Standard institutional parameters
    // =========================================================================
    void loadBalanced() {
        daily_loss_limit_ = -200.0;  // NZD - HARDCODED HARD STOP
        max_drawdown_pct_ = 10.0;
        max_exposure_ = 0.05;
        max_positions_ = 3;
        
        // v4.11.0: Class 0 reserved (crypto removed)
        
        // Forex
        asset_classes_[1].default_size = 0.01;
        asset_classes_[1].default_sl_bps = 15;
        asset_classes_[1].default_tp_bps = 30;
        asset_classes_[1].default_max_spread_bps = 2;
        asset_classes_[1].default_vpin = 0.65;
        asset_classes_[1].default_cooldown_ms = 500;
        
        // Metals
        asset_classes_[2].default_size = 0.01;
        asset_classes_[2].default_sl_bps = 20;
        asset_classes_[2].default_tp_bps = 40;
        asset_classes_[2].default_max_spread_bps = 3;
        asset_classes_[2].default_vpin = 0.60;
        asset_classes_[2].default_cooldown_ms = 300;
        
        // Indices
        asset_classes_[3].default_size = 0.1;
        asset_classes_[3].default_sl_bps = 10;
        asset_classes_[3].default_tp_bps = 20;
        asset_classes_[3].default_max_spread_bps = 2;
        asset_classes_[3].default_vpin = 0.70;
        asset_classes_[3].default_cooldown_ms = 200;
    }
    
    // =========================================================================
    // AGGRESSIVE - Wider stops, larger size, looser filters
    // =========================================================================
    void loadAggressive() {
        daily_loss_limit_ = -1000.0;
        max_drawdown_pct_ = 15.0;
        max_exposure_ = 0.10;
        max_positions_ = 5;
        
        // v4.11.0: Class 0 reserved (crypto removed)
        
        // Forex - aggressive
        asset_classes_[1].default_size = 0.02;
        asset_classes_[1].default_sl_bps = 20;
        asset_classes_[1].default_tp_bps = 40;
        asset_classes_[1].default_max_spread_bps = 3;
        asset_classes_[1].default_vpin = 0.75;
        asset_classes_[1].default_cooldown_ms = 300;
        
        // Metals - aggressive
        asset_classes_[2].default_size = 0.02;
        asset_classes_[2].default_sl_bps = 30;
        asset_classes_[2].default_tp_bps = 60;
        asset_classes_[2].default_max_spread_bps = 5;
        asset_classes_[2].default_vpin = 0.70;
        asset_classes_[2].default_cooldown_ms = 200;
        
        // Indices - aggressive
        asset_classes_[3].default_size = 0.2;
        asset_classes_[3].default_sl_bps = 15;
        asset_classes_[3].default_tp_bps = 30;
        asset_classes_[3].default_max_spread_bps = 3;
        asset_classes_[3].default_vpin = 0.80;
        asset_classes_[3].default_cooldown_ms = 100;
    }
    
    // =========================================================================
    // Apply asset class defaults to all symbols
    // =========================================================================
    void applyAssetClassDefaults() {
        for (int i = 0; i < symbol_count_; i++) {
            int ac = getAssetClassForSymbol(symbols_[i].symbol);
            if (ac >= 0 && ac < NUM_ASSET_CLASSES) {
                const AssetClassConfig& cfg = asset_classes_[ac];
                symbols_[i].position_size = cfg.default_size;
                symbols_[i].stop_loss_bps = cfg.default_sl_bps;
                symbols_[i].take_profit_bps = cfg.default_tp_bps;
                symbols_[i].max_spread_bps = cfg.default_max_spread_bps;
                symbols_[i].vpin_threshold = cfg.default_vpin;
                symbols_[i].ofi_threshold = cfg.default_ofi;
                symbols_[i].cooldown_ms = cfg.default_cooldown_ms;
            }
        }
    }
    
    // v4.11.0: Crypto removed - CFD only
    int getAssetClassForSymbol(const char* symbol) const {
        if (strstr(symbol, "XAU") || strstr(symbol, "XAG")) return 2;  // Metals
        if (strstr(symbol, "US30") || strstr(symbol, "NAS") || 
            strstr(symbol, "SPX") || strstr(symbol, "DAX")) return 3;  // Indices
        return 1;  // Default forex
    }
    
    // =========================================================================
    // JSON helpers
    // =========================================================================
    std::string symbolToJSON(const SymbolConfig& s) const {
        char buf[1024];
        snprintf(buf, sizeof(buf),
            "{\"symbol\":\"%s\",\"enabled\":%s,"
            "\"position_size\":%.6f,\"max_position\":%.4f,\"risk_pct\":%.2f,"
            "\"sl_bps\":%.1f,\"tp_bps\":%.1f,\"max_spread_bps\":%.2f,"
            "\"vpin\":%.2f,\"ofi\":%.2f,\"cooldown_ms\":%d,\"max_latency_us\":%d}",
            s.symbol, s.enabled ? "true" : "false",
            s.position_size, s.max_position, s.risk_per_trade_pct,
            s.stop_loss_bps, s.take_profit_bps, s.max_spread_bps,
            s.vpin_threshold, s.ofi_threshold, s.cooldown_ms, s.max_latency_us
        );
        return std::string(buf);
    }
    
    std::string assetClassToJSON(const AssetClassConfig& ac) const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"name\":\"%s\",\"asset_class\":%d,"
            "\"size\":%.6f,\"sl_bps\":%.1f,\"tp_bps\":%.1f,"
            "\"max_spread_bps\":%.2f,\"vpin\":%.2f,\"ofi\":%.2f,\"cooldown_ms\":%d}",
            ac.name, ac.asset_class,
            ac.default_size, ac.default_sl_bps, ac.default_tp_bps,
            ac.default_max_spread_bps, ac.default_vpin, ac.default_ofi, ac.default_cooldown_ms
        );
        return std::string(buf);
    }
    
    // =========================================================================
    // Config Persistence - Save/Load to file (PUBLIC)
    // =========================================================================
public:
    bool saveToFile(const char* path = "chimera_config.json") const {
        std::lock_guard<std::mutex> lock(mutex_);
        FILE* f = fopen(path, "w");
        if (!f) {
            fprintf(stderr, "[TradingConfig] Failed to open %s for writing\n", path);
            return false;
        }
        
        std::string json = toJSON_unlocked();
        fwrite(json.c_str(), 1, json.size(), f);
        fclose(f);
        
        printf("[TradingConfig] Saved to %s (%zu bytes)\n", path, json.size());
        return true;
    }
    
    bool loadFromFile(const char* path = "chimera_config.json") {
        FILE* f = fopen(path, "r");
        if (!f) {
            printf("[TradingConfig] No config file found at %s, using defaults\n", path);
            return false;
        }
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size <= 0 || size > 1000000) {
            fclose(f);
            return false;
        }
        
        std::string json(size, '\0');
        size_t bytesRead = fread(&json[0], 1, size, f);
        fclose(f);
        
        if (bytesRead != static_cast<size_t>(size)) {
            fprintf(stderr, "[TradingConfig] Incomplete read from %s\n", path);
            return false;
        }
        
        // Parse JSON (minimal parser)
        if (!parseJSON(json)) {
            fprintf(stderr, "[TradingConfig] Failed to parse %s\n", path);
            return false;
        }
        
        printf("[TradingConfig] Loaded from %s\n", path);
        return true;
    }
    
private:
    // toJSON without lock (for saveToFile which already holds lock)
    std::string toJSON_unlocked() const {
        std::string json = "{";
        json += "\"risk_level\":" + std::to_string(static_cast<int>(risk_level_)) + ",";
        json += "\"daily_loss_limit\":" + std::to_string(daily_loss_limit_) + ",";
        json += "\"max_drawdown_pct\":" + std::to_string(max_drawdown_pct_) + ",";
        json += "\"max_exposure\":" + std::to_string(max_exposure_) + ",";
        json += "\"max_positions\":" + std::to_string(max_positions_) + ",";
        json += "\"asset_classes\":[";
        for (int i = 0; i < NUM_ASSET_CLASSES; i++) {
            if (i > 0) json += ",";
            json += assetClassToJSON(asset_classes_[i]);
        }
        json += "],\"symbols\":[";
        for (int i = 0; i < symbol_count_; i++) {
            if (i > 0) json += ",";
            json += symbolToJSON(symbols_[i]);
        }
        json += "]}";
        return json;
    }
    
    // Minimal JSON parser for config reload
    bool parseJSON(const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Parse risk_level
        size_t pos = json.find("\"risk_level\":");
        if (pos != std::string::npos) {
            int val = atoi(json.c_str() + pos + 13);
            risk_level_ = static_cast<RiskLevel>(std::clamp(val, 0, 2));
        }
        
        // Parse global settings
        pos = json.find("\"daily_loss_limit\":");
        if (pos != std::string::npos) daily_loss_limit_ = atof(json.c_str() + pos + 19);
        
        pos = json.find("\"max_drawdown_pct\":");
        if (pos != std::string::npos) max_drawdown_pct_ = atof(json.c_str() + pos + 19);
        
        pos = json.find("\"max_exposure\":");
        if (pos != std::string::npos) max_exposure_ = atof(json.c_str() + pos + 15);
        
        pos = json.find("\"max_positions\":");
        if (pos != std::string::npos) max_positions_ = atoi(json.c_str() + pos + 16);
        
        return true;
    }
};

// Global config instance
inline TradingConfig& getTradingConfig() {
    static TradingConfig instance;
    return instance;
}

} // namespace Chimera
