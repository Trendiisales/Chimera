#pragma once
// =============================================================================
// FIXMessage.hpp - FIX Message Builder and Parser
// =============================================================================
// CHIMERA HFT - Zero-Copy FIX Message Handling
// HOT PATH: Use parseZeroCopy() + getView() - NO ALLOCATIONS
// COLD PATH: Use set() / get() / encode() for setup
// =============================================================================

#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <array>
#include <chrono>
#include "FIXConfig.hpp"

namespace Chimera {

// =============================================================================
// FIX FIELD VIEW - Zero-Copy Field Access
// =============================================================================
struct FIXFieldView {
    const char* ptr;   // Points directly into FIX buffer
    uint32_t    len;   // Length of field value
    
    bool valid() const noexcept { return ptr != nullptr && len > 0; }
    
    bool equals(char c) const noexcept {
        return len == 1 && ptr[0] == c;
    }
    
    bool equals(const char* s, uint32_t slen) const noexcept {
        if (len != slen) return false;
        return std::memcmp(ptr, s, len) == 0;
    }
};

// =============================================================================
// FAST NUMERIC PARSERS - NO ALLOCATION, NO LOCALE
// =============================================================================
inline int fast_parse_int(const char* p, uint32_t n) noexcept {
    if (!p || n == 0) return 0;
    int v = 0;
    bool neg = false;
    uint32_t i = 0;
    if (p[0] == '-') { neg = true; i = 1; }
    else if (p[0] == '+') { i = 1; }
    for (; i < n; ++i) {
        char c = p[i];
        if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
    }
    return neg ? -v : v;
}

inline int64_t fast_parse_int64(const char* p, uint32_t n) noexcept {
    if (!p || n == 0) return 0;
    int64_t v = 0;
    bool neg = false;
    uint32_t i = 0;
    if (p[0] == '-') { neg = true; i = 1; }
    else if (p[0] == '+') { i = 1; }
    for (; i < n; ++i) {
        char c = p[i];
        if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
    }
    return neg ? -v : v;
}

inline uint32_t fast_parse_uint(const char* p, uint32_t n) noexcept {
    if (!p || n == 0) return 0;
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
    }
    return v;
}

inline double fast_parse_double(const char* p, uint32_t n) noexcept {
    if (!p || n == 0) return 0.0;
    double v = 0.0;
    double frac = 0.1;
    bool neg = false;
    bool seen_dot = false;
    uint32_t i = 0;
    if (p[0] == '-') { neg = true; i = 1; }
    else if (p[0] == '+') { i = 1; }
    for (; i < n; ++i) {
        char c = p[i];
        if (c == '.') {
            seen_dot = true;
        } else if (c >= '0' && c <= '9') {
            if (!seen_dot) {
                v = v * 10.0 + (c - '0');
            } else {
                v += frac * (c - '0');
                frac *= 0.1;
            }
        }
    }
    return neg ? -v : v;
}

// =============================================================================
// FIX MESSAGE CLASS
// =============================================================================
class FIXMessage {
public:
    static constexpr char SOH = '\x01';          // FIX delimiter
    static constexpr size_t MAX_FIELDS = 128;    // Max fields to index
    static constexpr size_t MAX_MSG_SIZE = 4096; // Max message size
    
    FIXMessage() : buf_(nullptr), buf_len_(0), field_count_(0) {
        clear();
    }
    
    void clear() noexcept {
        buf_ = nullptr;
        buf_len_ = 0;
        field_count_ = 0;
        body_.clear();
    }
    
    // =========================================================================
    // COLD PATH API - Message Building (allocates strings)
    // =========================================================================
    
    void setMsgType(char type) {
        setField(FIXTag::MsgType, std::string(1, type));
    }
    
    void setMsgType(const char* type) {
        setField(FIXTag::MsgType, std::string(type));
    }
    
    void setField(int tag, const std::string& value) {
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "%d=", tag);
        body_.append(buf, len);
        body_.append(value);
        body_.push_back(SOH);
    }
    
    void setField(int tag, int value) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), "%d=%d%c", tag, value, SOH);
        body_.append(buf, len);
    }
    
    void setField(int tag, uint32_t value) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), "%d=%u%c", tag, value, SOH);
        body_.append(buf, len);
    }
    
    void setField(int tag, double value, int precision = 5) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), "%d=%.*f%c", tag, precision, value, SOH);
        body_.append(buf, len);
    }
    
    void setField(int tag, char value) {
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "%d=%c%c", tag, value, SOH);
        body_.append(buf, len);
    }
    
    // Set current timestamp in FIX format: YYYYMMDD-HH:MM:SS.sss
    void setSendingTime() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        
        std::tm tm_now;
#ifdef _WIN32
        gmtime_s(&tm_now, &time_t_now);
#else
        gmtime_r(&time_t_now, &tm_now);
#endif
        
        // cTrader requires NO milliseconds in timestamp
        // Working format: 20251216-00:30:24 (not 20251216-00:30:24.123)
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), 
            "%04d%02d%02d-%02d:%02d:%02d",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        
        setField(FIXTag::SendingTime, std::string(buf, len));
    }
    
    // Encode complete FIX message with header and checksum
    std::string encode(const std::string& senderCompID,
                       const std::string& targetCompID,
                       uint32_t seqNum,
                       const std::string& senderSubID = "") const {
        
        // Build header
        std::string header;
        header.reserve(256);
        
        // MsgType must be first in body, followed by other header fields
        char buf[64];
        
        // SenderCompID
        int len = std::snprintf(buf, sizeof(buf), "49=%s%c", senderCompID.c_str(), SOH);
        header.append(buf, len);
        
        // TargetCompID
        len = std::snprintf(buf, sizeof(buf), "56=%s%c", targetCompID.c_str(), SOH);
        header.append(buf, len);
        
        // MsgSeqNum
        len = std::snprintf(buf, sizeof(buf), "34=%u%c", seqNum, SOH);
        header.append(buf, len);
        
        // SenderSubID ONLY - cTrader does NOT accept TargetSubID (57)
        if (!senderSubID.empty()) {
            // Tag 50 - SenderSubID (QUOTE or TRADE) - REQUIRED
            len = std::snprintf(buf, sizeof(buf), "50=%s%c", senderSubID.c_str(), SOH);
            header.append(buf, len);
            // NOTE: DO NOT SEND 57 (TargetSubID) - cTrader silently drops!
        }
        
        // Combine header + body
        std::string full_body = body_.substr(0, body_.find(SOH) + 1);  // MsgType first
        full_body += header;
        full_body += body_.substr(body_.find(SOH) + 1);  // Rest of body
        
        // Calculate body length (everything after 9= until checksum)
        size_t body_len = full_body.length();
        
        // Build final message
        std::string msg;
        msg.reserve(body_len + 32);
        
        // BeginString
        msg.append("8=FIX.4.4");
        msg.push_back(SOH);
        
        // BodyLength
        len = std::snprintf(buf, sizeof(buf), "9=%zu%c", body_len, SOH);
        msg.append(buf, len);
        
        // Body
        msg.append(full_body);
        
        // Calculate checksum
        uint32_t checksum = 0;
        for (char c : msg) {
            checksum += static_cast<uint8_t>(c);
        }
        checksum %= 256;
        
        // Append checksum
        len = std::snprintf(buf, sizeof(buf), "10=%03u%c", checksum, SOH);
        msg.append(buf, len);
        
        return msg;
    }
    
    // =========================================================================
    // HOT PATH API - Zero-Copy Parsing
    // =========================================================================
    
    // Parse FIX message into zero-copy index
    bool parseZeroCopy(const char* data, uint32_t len) noexcept {
        buf_ = data;
        buf_len_ = len;
        field_count_ = 0;
        
        uint32_t pos = 0;
        while (pos < len && field_count_ < MAX_FIELDS) {
            // Find tag
            uint32_t tag = 0;
            while (pos < len && data[pos] != '=') {
                if (data[pos] >= '0' && data[pos] <= '9') {
                    tag = tag * 10 + (data[pos] - '0');
                }
                pos++;
            }
            
            if (pos >= len) break;
            pos++;  // Skip '='
            
            // Find value
            uint32_t val_start = pos;
            while (pos < len && data[pos] != SOH) {
                pos++;
            }
            
            uint32_t val_len = pos - val_start;
            
            // Index this field
            if (field_count_ < MAX_FIELDS) {
                field_index_[field_count_].tag = tag;
                field_index_[field_count_].offset = val_start;
                field_index_[field_count_].length = val_len;
                field_count_++;
            }
            
            pos++;  // Skip SOH
        }
        
        return field_count_ > 0;
    }
    
    // Get field view by tag (zero-copy)
    bool getView(int tag, FIXFieldView& out) const noexcept {
        for (uint32_t i = 0; i < field_count_; ++i) {
            if (field_index_[i].tag == static_cast<uint32_t>(tag)) {
                out.ptr = buf_ + field_index_[i].offset;
                out.len = field_index_[i].length;
                return true;
            }
        }
        out.ptr = nullptr;
        out.len = 0;
        return false;
    }
    
    // Check if field exists
    bool hasField(int tag) const noexcept {
        for (uint32_t i = 0; i < field_count_; ++i) {
            if (field_index_[i].tag == static_cast<uint32_t>(tag)) {
                return true;
            }
        }
        return false;
    }
    
    // Get integer field (hot path)
    int getIntFast(int tag) const noexcept {
        FIXFieldView v;
        if (getView(tag, v)) {
            return fast_parse_int(v.ptr, v.len);
        }
        return 0;
    }
    
    // Get double field (hot path)
    double getDoubleFast(int tag) const noexcept {
        FIXFieldView v;
        if (getView(tag, v)) {
            return fast_parse_double(v.ptr, v.len);
        }
        return 0.0;
    }
    
    // Get string field (cold path - allocates)
    std::string getString(int tag) const {
        FIXFieldView v;
        if (getView(tag, v)) {
            return std::string(v.ptr, v.len);
        }
        return "";
    }
    
    // Check message type
    bool isMsgType(char c) const noexcept {
        FIXFieldView v;
        if (getView(FIXTag::MsgType, v)) {
            return v.equals(c);
        }
        return false;
    }
    
    // Check two-char message type
    bool isMsgType(const char* type) const noexcept {
        FIXFieldView v;
        if (getView(FIXTag::MsgType, v)) {
            return v.equals(type, std::strlen(type));
        }
        return false;
    }
    
    // Get message type as char
    char getMsgType() const noexcept {
        FIXFieldView v;
        if (getView(FIXTag::MsgType, v) && v.len > 0) {
            return v.ptr[0];
        }
        return '\0';
    }
    
    // Access raw buffer
    const char* buffer() const noexcept { return buf_; }
    uint32_t bufferLen() const noexcept { return buf_len_; }
    
private:
    // Zero-copy buffer reference
    const char* buf_;
    uint32_t    buf_len_;
    
    // Field index for zero-copy access
    struct FieldEntry {
        uint32_t tag;
        uint32_t offset;
        uint32_t length;
    };
    std::array<FieldEntry, MAX_FIELDS> field_index_;
    uint32_t field_count_;
    
    // Message body for building (cold path)
    std::string body_;
};

// =============================================================================
// FIX MESSAGE BUILDER HELPERS
// =============================================================================

// Build Logon message - EXACT format for cTrader:
// 8=FIX.4.4|9=XXX|35=A|49=2067070|56=cServer|34=1|52=20251222-00:00:00|57=QUOTE|50=QUOTE|98=0|108=10|141=Y|1137=9|553=2067070|554=PASSWORD|10=XXX|
// CRITICAL: Tag 49 (SenderCompID) MUST be account ID only (2067070), NOT demo.blackbull.2067070
inline std::string buildLogonMessage(const FIXConfig& cfg, 
                                      uint32_t seqNum,
                                      const std::string& senderSubID,
                                      bool resetSeqNum = false) {
    constexpr char SOH = '\x01';
    
    // Get timestamp (NO milliseconds)
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
#ifdef _WIN32
    gmtime_s(&tm_now, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_now);
#endif
    char timestamp[64];
    std::snprintf(timestamp, sizeof(timestamp), 
        "%04d%02d%02d-%02d:%02d:%02d",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    
    // Build body in EXACT working order:
    // 35=A|49=sender|56=target|34=seq|52=time|57=subID|50=subID|98=0|108=30|141=Y|553=user|554=pass|
    std::string body;
    body.reserve(256);
    
    char buf[64];
    int len;
    
    // 35=A (MsgType)
    body += "35=A";
    body += SOH;
    
    // 49=SenderCompID
    len = std::snprintf(buf, sizeof(buf), "49=%s%c", cfg.senderCompID.c_str(), SOH);
    body.append(buf, len);
    
    // 56=TargetCompID
    len = std::snprintf(buf, sizeof(buf), "56=%s%c", cfg.targetCompID.c_str(), SOH);
    body.append(buf, len);
    
    // 34=MsgSeqNum
    len = std::snprintf(buf, sizeof(buf), "34=%u%c", seqNum, SOH);
    body.append(buf, len);
    
    // 52=SendingTime
    len = std::snprintf(buf, sizeof(buf), "52=%s%c", timestamp, SOH);
    body.append(buf, len);
    
    // 57=TargetSubID (TRADE or QUOTE) - REQUIRED per Dec 16 working message
    len = std::snprintf(buf, sizeof(buf), "57=%s%c", senderSubID.c_str(), SOH);
    body.append(buf, len);
    
    // 50=SenderSubID (TRADE or QUOTE) - REQUIRED
    len = std::snprintf(buf, sizeof(buf), "50=%s%c", senderSubID.c_str(), SOH);
    body.append(buf, len);
    
    // 98=EncryptMethod
    body += "98=0";
    body += SOH;
    
    // 108=HeartBtInt (MUST be 30 for cTrader)
    body += "108=30";
    body += SOH;
    
    // 141=ResetSeqNumFlag (ONLY on cold start, not reconnects)
    if (resetSeqNum) {
        body += "141=Y";
        body += SOH;
    }
    
    // NOTE: DO NOT SEND 1137 - it is ILLEGAL in FIX.4.4
    
    // 553=Username - MUST be numeric account ID (2067070), NOT SenderCompID!
    // Dec 16 working: 553=2067070
    len = std::snprintf(buf, sizeof(buf), "553=%s%c", cfg.username.c_str(), SOH);
    body.append(buf, len);
    
    // 554=Password
    len = std::snprintf(buf, sizeof(buf), "554=%s%c", cfg.password.c_str(), SOH);
    body.append(buf, len);
    
    // Build final message
    std::string msg;
    msg.reserve(body.length() + 32);
    
    // 8=BeginString
    msg += "8=FIX.4.4";
    msg += SOH;
    
    // 9=BodyLength
    len = std::snprintf(buf, sizeof(buf), "9=%zu%c", body.length(), SOH);
    msg.append(buf, len);
    
    // Body
    msg += body;
    
    // Calculate checksum
    uint32_t checksum = 0;
    for (char c : msg) {
        checksum += static_cast<uint8_t>(c);
    }
    checksum %= 256;
    
    // 10=CheckSum
    len = std::snprintf(buf, sizeof(buf), "10=%03u%c", checksum, SOH);
    msg.append(buf, len);
    
    return msg;
}

// Build Logout message
inline std::string buildLogoutMessage(const FIXConfig& cfg,
                                       uint32_t seqNum,
                                       const std::string& senderSubID,
                                       const std::string& text = "") {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::Logout);
    msg.setSendingTime();
    if (!text.empty()) {
        msg.setField(FIXTag::Text, text);
    }
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build Heartbeat message
inline std::string buildHeartbeatMessage(const FIXConfig& cfg,
                                          uint32_t seqNum,
                                          const std::string& senderSubID,
                                          const std::string& testReqID = "") {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::Heartbeat);
    msg.setSendingTime();
    if (!testReqID.empty()) {
        msg.setField(FIXTag::TestReqID, testReqID);
    }
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build TestRequest message
inline std::string buildTestRequestMessage(const FIXConfig& cfg,
                                            uint32_t seqNum,
                                            const std::string& senderSubID,
                                            const std::string& testReqID) {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::TestRequest);
    msg.setSendingTime();
    msg.setField(FIXTag::TestReqID, testReqID);
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build ResendRequest message
inline std::string buildResendRequestMessage(const FIXConfig& cfg,
                                              uint32_t seqNum,
                                              const std::string& senderSubID,
                                              uint32_t beginSeq,
                                              uint32_t endSeq) {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::ResendRequest);
    msg.setSendingTime();
    msg.setField(FIXTag::BeginSeqNo, beginSeq);
    msg.setField(FIXTag::EndSeqNo, endSeq);
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build MarketDataRequest message
inline std::string buildMarketDataRequestMessage(const FIXConfig& cfg,
                                                  uint32_t seqNum,
                                                  const std::string& senderSubID,
                                                  const std::string& mdReqID,
                                                  const std::string& symbolOrSecurityId,
                                                  int depth = 1,
                                                  bool subscribe = true,
                                                  bool /*useSecurityId*/ = true) {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::MarketDataRequest);
    msg.setSendingTime();
    msg.setField(FIXTag::MDReqID, mdReqID);
    msg.setField(FIXTag::SubscriptionRequestType, subscribe ? '1' : '2');  // 1=Subscribe, 2=Unsubscribe
    msg.setField(FIXTag::MarketDepth, depth);
    msg.setField(FIXTag::MDUpdateType, 0);  // 0=Full refresh
    
    // Entry types: 0=Bid, 1=Offer
    msg.setField(FIXTag::NoMDEntryTypes, 2);
    msg.setField(FIXTag::MDEntryType, '0');  // Bid
    msg.setField(FIXTag::MDEntryType, '1');  // Offer
    
    // Symbol specification
    msg.setField(FIXTag::NoRelatedSym, 1);
    
    // cTrader wants the NUMERIC ID in tag 55 (Symbol), NOT in tag 48 (SecurityID)
    // Tag 48 is rejected: "Tag not defined for this message type"
    // So we always use tag 55 with either the numeric ID or symbol name
    msg.setField(FIXTag::Symbol, symbolOrSecurityId);
    
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build SecurityListRequest message (35=x)
inline std::string buildSecurityListRequestMessage(const FIXConfig& cfg,
                                                    uint32_t seqNum,
                                                    const std::string& senderSubID,
                                                    const std::string& securityReqID) {
    FIXMessage msg;
    msg.setMsgType('x');  // SecurityListRequest
    msg.setSendingTime();
    msg.setField(FIXTag::SecurityReqID, securityReqID);
    msg.setField(FIXTag::SecurityListRequestType, 0);  // 0 = All Securities
    
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build NewOrderSingle message
// v6.88 FIX: Added positionEffect parameter - CRITICAL for cTrader CFDs
inline std::string buildNewOrderSingleMessage(const FIXConfig& cfg,
                                               uint32_t seqNum,
                                               const std::string& senderSubID,
                                               const std::string& clOrdID,
                                               const std::string& symbol,
                                               char side,
                                               double qty,
                                               char ordType = FIXOrdType::Market,
                                               double price = 0.0,
                                               char timeInForce = FIXTimeInForce::IOC,
                                               char positionEffect = FIXPositionEffect::Open) {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::NewOrderSingle);
    msg.setSendingTime();
    msg.setField(FIXTag::ClOrdID, clOrdID);
    msg.setField(FIXTag::Symbol, symbol);
    msg.setField(FIXTag::Side, side);
    msg.setField(FIXTag::OrderQty, qty, 2);
    msg.setField(FIXTag::OrdType, ordType);
    msg.setField(FIXTag::TimeInForce, timeInForce);
    
    // v6.88 FIX: PositionEffect (Tag 77) - REQUIRED for cTrader CFDs
    // O = Open new position, C = Close existing position
    msg.setField(FIXTag::PositionEffect, positionEffect);
    
    // Set TransactTime (NO milliseconds for cTrader)
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_now;
#ifdef _WIN32
    gmtime_s(&tm_now, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_now);
#endif
    
    char buf[64];
    std::snprintf(buf, sizeof(buf), 
        "%04d%02d%02d-%02d:%02d:%02d",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    msg.setField(FIXTag::TransactTime, std::string(buf));
    
    if (ordType == FIXOrdType::Limit || ordType == FIXOrdType::StopLimit) {
        msg.setField(FIXTag::Price, price, 5);
    }
    
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

// Build OrderCancelRequest message
inline std::string buildOrderCancelRequestMessage(const FIXConfig& cfg,
                                                   uint32_t seqNum,
                                                   const std::string& senderSubID,
                                                   const std::string& clOrdID,
                                                   const std::string& origClOrdID,
                                                   const std::string& symbol,
                                                   char side) {
    FIXMessage msg;
    msg.setMsgType(FIXMsgType::OrderCancelRequest);
    msg.setSendingTime();
    msg.setField(FIXTag::ClOrdID, clOrdID);
    msg.setField(11, origClOrdID);  // OrigClOrdID = tag 41
    msg.setField(FIXTag::Symbol, symbol);
    msg.setField(FIXTag::Side, side);
    
    // Set TransactTime (NO milliseconds for cTrader)
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_now;
#ifdef _WIN32
    gmtime_s(&tm_now, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_now);
#endif
    
    char buf[32];
    std::snprintf(buf, sizeof(buf), 
        "%04d%02d%02d-%02d:%02d:%02d",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    msg.setField(FIXTag::TransactTime, std::string(buf));
    
    return msg.encode(cfg.senderCompID, cfg.targetCompID, seqNum, senderSubID);
}

} // namespace Chimera
