#pragma once
// =============================================================================
// ConfigLoader.hpp - INI File Parser for Chimera Configuration
// =============================================================================
// Loads settings from config.ini - NO HARDCODED CREDENTIALS
// =============================================================================

#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <cstdlib>

namespace Chimera {

class ConfigLoader {
public:
    static ConfigLoader& instance() {
        static ConfigLoader inst;
        return inst;
    }
    
    bool load(const std::string& path = "config.ini") {
        // Try multiple paths
        std::vector<std::string> paths = {
            path,
            "../config.ini",
            "../../config.ini",
            std::string(getenv("HOME") ? getenv("HOME") : ".") + "/Chimera/config.ini"
        };
        
        for (const auto& p : paths) {
            std::ifstream file(p);
            if (file.is_open()) {
                configPath_ = p;
                return parse(file);
            }
        }
        
        std::cerr << "[ConfigLoader] ERROR: config.ini not found!\n";
        std::cerr << "[ConfigLoader] Searched paths:\n";
        for (const auto& p : paths) {
            std::cerr << "  - " << p << "\n";
        }
        return false;
    }
    
    std::string get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const {
        std::string fullKey = section + "." + key;
        auto it = values_.find(fullKey);
        if (it != values_.end()) {
            return it->second;
        }
        return defaultVal;
    }
    
    int getInt(const std::string& section, const std::string& key, int defaultVal = 0) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        try {
            return std::stoi(val);
        } catch (...) {
            return defaultVal;
        }
    }
    
    double getDouble(const std::string& section, const std::string& key, double defaultVal = 0.0) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        try {
            return std::stod(val);
        } catch (...) {
            return defaultVal;
        }
    }
    
    bool getBool(const std::string& section, const std::string& key, bool defaultVal = false) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        return (val == "true" || val == "1" || val == "yes" || val == "on");
    }
    
    const std::string& getConfigPath() const { return configPath_; }
    
    void dump() const {
        std::cout << "[ConfigLoader] Loaded from: " << configPath_ << "\n";
        std::cout << "[ConfigLoader] Values:\n";
        for (const auto& kv : values_) {
            // Mask passwords
            if (kv.first.find("password") != std::string::npos ||
                kv.first.find("secret") != std::string::npos) {
                std::cout << "  " << kv.first << " = ********\n";
            } else {
                std::cout << "  " << kv.first << " = " << kv.second << "\n";
            }
        }
    }

private:
    ConfigLoader() = default;
    
    bool parse(std::ifstream& file) {
        std::string line;
        std::string currentSection;
        
        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            }
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            
            // Section header
            if (line[0] == '[') {
                size_t closePos = line.find(']');
                if (closePos != std::string::npos) {
                    currentSection = line.substr(1, closePos - 1);
                }
                continue;
            }
            
            // Key = Value
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string value = line.substr(eqPos + 1);
                
                // Trim key
                end = key.find_last_not_of(" \t");
                if (end != std::string::npos) key = key.substr(0, end + 1);
                
                // Trim value
                start = value.find_first_not_of(" \t");
                if (start != std::string::npos) value = value.substr(start);
                end = value.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) value = value.substr(0, end + 1);
                
                // Store with section prefix
                std::string fullKey = currentSection + "." + key;
                values_[fullKey] = value;
            }
        }
        
        return !values_.empty();
    }
    
    std::unordered_map<std::string, std::string> values_;
    std::string configPath_;
};

} // namespace Chimera
