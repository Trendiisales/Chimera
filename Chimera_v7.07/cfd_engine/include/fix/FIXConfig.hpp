#pragma once
// =============================================================================
// FIXConfig.hpp - cTrader FIX Configuration
// =============================================================================
// CHIMERA HFT - CFD Engine Configuration
// ALL CREDENTIALS LOADED FROM config.ini - NOTHING HARDCODED
// =============================================================================

#include <string>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstdlib>

namespace Chimera {

// =============================================================================
// INLINE CONFIG LOADER (to avoid circular dependencies)
// =============================================================================
class ConfigLoader {
public:
    static ConfigLoader& instance() {
        static ConfigLoader inst;
        return inst;
    }
    
    bool load(const std::string& path = "config.ini") {
        if (loaded_) return true;
        
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
                loaded_ = parse(file);
                return loaded_;
            }
        }
        
        std::cerr << "[ConfigLoader] ERROR: config.ini not found!\n";
        return false;
    }
    
    std::string get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const {
        std::string fullKey = section + "." + key;
        auto it = values_.find(fullKey);
        return (it != values_.end()) ? it->second : defaultVal;
    }
    
    int getInt(const std::string& section, const std::string& key, int defaultVal = 0) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        try { return std::stoi(val); } catch (...) { return defaultVal; }
    }
    
    double getDouble(const std::string& section, const std::string& key, double defaultVal = 0.0) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        try { return std::stod(val); } catch (...) { return defaultVal; }
    }
    
    bool getBool(const std::string& section, const std::string& key, bool defaultVal = false) const {
        std::string val = get(section, key);
        if (val.empty()) return defaultVal;
        return (val == "true" || val == "1" || val == "yes" || val == "on");
    }
    
    const std::string& getConfigPath() const { return configPath_; }
    bool isLoaded() const { return loaded_; }

private:
    ConfigLoader() = default;
    
    bool parse(std::ifstream& file) {
        std::string line, currentSection;
        
        while (std::getline(file, line)) {
            // Trim
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) line = line.substr(0, end + 1);
            
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            
            if (line[0] == '[') {
                size_t closePos = line.find(']');
                if (closePos != std::string::npos) {
                    currentSection = line.substr(1, closePos - 1);
                }
                continue;
            }
            
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string value = line.substr(eqPos + 1);
                
                // Trim key and value
                end = key.find_last_not_of(" \t");
                if (end != std::string::npos) key = key.substr(0, end + 1);
                start = value.find_first_not_of(" \t");
                if (start != std::string::npos) value = value.substr(start);
                end = value.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) value = value.substr(0, end + 1);
                
                values_[currentSection + "." + key] = value;
            }
        }
        return !values_.empty();
    }
    
    std::unordered_map<std::string, std::string> values_;
    std::string configPath_;
    bool loaded_ = false;
};

// =============================================================================
// FIX SESSION CONFIGURATION
// =============================================================================
struct FIXConfig {
    // Connection Settings
    std::string host;
    uint16_t    pricePort;
    uint16_t    tradePort;
    bool        useSSL;
    
    // Session Identification
    std::string senderCompID;
    std::string targetCompID;
    std::string senderSubID_Quote;
    std::string senderSubID_Trade;
    
    // Authentication
    std::string username;
    std::string password;
    
    // Heartbeat Settings
    uint32_t    heartbeatIntervalSec;
    uint32_t    reconnectDelaySec;
    uint32_t    maxReconnectAttempts;
    
    // Sequence Numbers
    uint32_t    outSeqNum;
    uint32_t    inSeqNum;
    
    // Trading Parameters
    double      maxOrderQty;
    double      minOrderQty;
    uint32_t    maxOrdersPerSecond;
    
    // Default constructor - LOADS FROM config.ini
    FIXConfig() {
        auto& cfg = ConfigLoader::instance();
        cfg.load();
        
        host                  = cfg.get("ctrader", "host", "");
        pricePort             = static_cast<uint16_t>(cfg.getInt("ctrader", "quote_port", 5211));
        tradePort             = static_cast<uint16_t>(cfg.getInt("ctrader", "trade_port", 5212));
        useSSL                = cfg.getBool("ctrader", "use_ssl", true);
        
        senderCompID          = cfg.get("ctrader", "sender_comp_id", "");
        targetCompID          = cfg.get("ctrader", "target_comp_id", "cServer");
        senderSubID_Quote     = "QUOTE";
        senderSubID_Trade     = "TRADE";
        
        username              = cfg.get("ctrader", "username", "");
        password              = cfg.get("ctrader", "password", "");
        
        heartbeatIntervalSec  = static_cast<uint32_t>(cfg.getInt("ctrader", "heartbeat_interval", 30));
        reconnectDelaySec     = static_cast<uint32_t>(cfg.getInt("ctrader", "reconnect_delay", 5));
        maxReconnectAttempts  = static_cast<uint32_t>(cfg.getInt("ctrader", "max_reconnect_attempts", 10));
        
        outSeqNum             = 1;
        inSeqNum              = 1;
        
        maxOrderQty           = cfg.getDouble("risk", "max_order_qty", 100.0);
        minOrderQty           = cfg.getDouble("risk", "min_order_qty", 0.01);
        maxOrdersPerSecond    = static_cast<uint32_t>(cfg.getInt("risk", "max_orders_per_second", 50));
    }
    
    bool isValid() const {
        if (host.empty()) { std::cerr << "[FIXConfig] ERROR: host is empty\n"; return false; }
        if (senderCompID.empty()) { std::cerr << "[FIXConfig] ERROR: sender_comp_id is empty\n"; return false; }
        if (username.empty()) { std::cerr << "[FIXConfig] ERROR: username is empty\n"; return false; }
        if (password.empty()) { std::cerr << "[FIXConfig] ERROR: password is empty\n"; return false; }
        return true;
    }
    
    void print() const {
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        std::cout << "  FIX Configuration (from config.ini)\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        std::cout << "  Host:           " << host << "\n";
        std::cout << "  TRADE Port:     " << tradePort << " (connects FIRST)\n";
        std::cout << "  QUOTE Port:     " << pricePort << " (connects AFTER)\n";
        std::cout << "  SenderCompID:   " << senderCompID << "\n";
        std::cout << "  TargetCompID:   " << targetCompID << "\n";
        std::cout << "  Username:       " << username << "\n";
        std::cout << "  Password:       ********\n";
        std::cout << "  HeartBtInt:     " << heartbeatIntervalSec << "s\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n";
    }
};

// =============================================================================
// FIX TAG CONSTANTS (FIX 4.4)
// =============================================================================
namespace FIXTag {
    constexpr int BeginString       = 8;
    constexpr int BodyLength        = 9;
    constexpr int MsgType           = 35;
    constexpr int SenderCompID      = 49;
    constexpr int TargetCompID      = 56;
    constexpr int MsgSeqNum         = 34;
    constexpr int SendingTime       = 52;
    constexpr int SenderSubID       = 50;
    constexpr int TargetSubID       = 57;
    constexpr int CheckSum          = 10;
    
    constexpr int EncryptMethod     = 98;
    constexpr int HeartBtInt        = 108;
    constexpr int ResetSeqNumFlag   = 141;
    constexpr int Username          = 553;
    constexpr int Password          = 554;
    
    constexpr int TestReqID         = 112;
    constexpr int RefSeqNum         = 45;
    constexpr int Text              = 58;
    constexpr int SessionRejectReason = 373;
    
    constexpr int BeginSeqNo        = 7;
    constexpr int EndSeqNo          = 16;
    
    constexpr int MDReqID           = 262;
    constexpr int SubscriptionRequestType = 263;
    constexpr int MarketDepth       = 264;
    constexpr int MDUpdateType      = 265;
    constexpr int NoMDEntryTypes    = 267;
    constexpr int MDEntryType       = 269;
    constexpr int NoRelatedSym      = 146;
    constexpr int Symbol            = 55;
    
    constexpr int NoMDEntries       = 268;
    constexpr int MDEntryPx         = 270;
    constexpr int MDEntrySize       = 271;
    constexpr int MDEntryDate       = 272;
    constexpr int MDEntryTime       = 273;
    
    constexpr int ClOrdID           = 11;
    constexpr int OrderID           = 37;
    constexpr int ExecID            = 17;
    constexpr int ExecType          = 150;
    constexpr int OrdStatus         = 39;
    constexpr int Side              = 54;
    constexpr int OrdType           = 40;
    constexpr int OrderQty          = 38;
    constexpr int Price             = 44;
    constexpr int StopPx            = 99;
    constexpr int TimeInForce       = 59;
    constexpr int TransactTime      = 60;
    constexpr int LeavesQty         = 151;
    constexpr int CumQty            = 14;
    constexpr int AvgPx             = 6;
    constexpr int LastPx            = 31;
    constexpr int LastQty           = 32;
    
    constexpr int PosReqID          = 710;
    constexpr int PosMaintRptID     = 721;
    constexpr int TotalNumPosReports = 727;
    constexpr int PosReqResult      = 728;
    constexpr int NoPositions       = 702;
    constexpr int PosType           = 703;
    constexpr int LongQty           = 704;
    constexpr int ShortQty          = 705;
    
    // v6.88 FIX: PositionEffect - CRITICAL for cTrader CFDs
    constexpr int PositionEffect    = 77;   // O=Open, C=Close
    
    // SecurityList tags
    constexpr int SecurityReqID     = 320;
    constexpr int SecurityID        = 48;
    constexpr int SecurityIDSource  = 22;
    constexpr int SecurityListRequestType = 559;
    constexpr int SecurityDesc      = 107;
    constexpr int LastFragment      = 893;
    constexpr int NoRelatedSecurities = 146;  // same as NoRelatedSym
}

namespace FIXMsgType {
    constexpr char Heartbeat        = '0';
    constexpr char TestRequest      = '1';
    constexpr char ResendRequest    = '2';
    constexpr char Reject           = '3';
    constexpr char SequenceReset    = '4';
    constexpr char Logout           = '5';
    constexpr char Logon            = 'A';
    constexpr char NewOrderSingle   = 'D';
    constexpr char OrderCancelRequest = 'F';
    constexpr char OrderStatusRequest = 'H';
    constexpr char ExecutionReport  = '8';
    constexpr char OrderCancelReject = '9';
    constexpr char MarketDataRequest = 'V';
    constexpr char MarketDataSnapshot = 'W';
    constexpr char MarketDataIncremental = 'X';
    constexpr char MarketDataReject = 'Y';
    constexpr char SecurityListRequest = 'x';
    constexpr char SecurityList     = 'y';
    constexpr const char* RequestForPositions = "AN";
    constexpr const char* PositionReport       = "AP";
}

namespace FIXSide {
    constexpr char Buy  = '1';
    constexpr char Sell = '2';
}

namespace FIXOrdType {
    constexpr char Market          = '1';
    constexpr char Limit           = '2';
    constexpr char Stop            = '3';
    constexpr char StopLimit       = '4';
}

namespace FIXTimeInForce {
    constexpr char Day              = '0';
    constexpr char GTC              = '1';
    constexpr char IOC              = '3';
    constexpr char FOK              = '4';
    constexpr char GTD              = '6';
}

// v6.88 FIX: PositionEffect values - CRITICAL for cTrader CFDs
namespace FIXPositionEffect {
    constexpr char Open   = 'O';  // Open new position
    constexpr char Close  = 'C';  // Close existing position
}

namespace FIXExecType {
    constexpr char New              = '0';
    constexpr char PartialFill      = '1';
    constexpr char Fill             = '2';
    constexpr char DoneForDay       = '3';
    constexpr char Canceled         = '4';
    constexpr char Replaced         = '5';
    constexpr char PendingCancel    = '6';
    constexpr char Stopped          = '7';
    constexpr char Rejected         = '8';
    constexpr char Suspended        = '9';
    constexpr char PendingNew       = 'A';
    constexpr char Calculated       = 'B';
    constexpr char Expired          = 'C';
    constexpr char Restated         = 'D';
    constexpr char PendingReplace   = 'E';
    constexpr char Trade            = 'F';
}

namespace FIXOrdStatus {
    constexpr char New              = '0';
    constexpr char PartiallyFilled  = '1';
    constexpr char Filled           = '2';
    constexpr char DoneForDay       = '3';
    constexpr char Canceled         = '4';
    constexpr char Replaced         = '5';
    constexpr char PendingCancel    = '6';
    constexpr char Stopped          = '7';
    constexpr char Rejected         = '8';
    constexpr char Suspended        = '9';
    constexpr char PendingNew       = 'A';
    constexpr char Calculated       = 'B';
    constexpr char Expired          = 'C';
    constexpr char AcceptedForBidding = 'D';
    constexpr char PendingReplace   = 'E';
}

} // namespace Chimera
