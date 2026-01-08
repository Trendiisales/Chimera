#pragma once
// =============================================================================
// CTRADER SHARED TYPES - Used by both FIX and Open API clients
// =============================================================================

#include <string>
#include <functional>
#include <cstdint>

namespace Chimera {

// =============================================================================
// ORDER SIDE (shared between FIX and Open API)
// =============================================================================
namespace OrderSide {
    constexpr char Buy = '1';
    constexpr char Sell = '2';
}

// Legacy alias for backward compatibility
namespace FIXSide = OrderSide;

// =============================================================================
// TICK DATA
// =============================================================================
struct CTraderTick {
    std::string symbol;
    double bid = 0.0;
    double ask = 0.0;
    double bidSize = 0.0;
    double askSize = 0.0;
    uint64_t timestamp = 0;
    
    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

// =============================================================================
// EXECUTION REPORT
// =============================================================================
namespace ExecType {
    constexpr char New = '0';
    constexpr char PartialFill = '1';
    constexpr char Fill = '2';
    constexpr char Canceled = '4';
    constexpr char Rejected = '8';
}

struct CTraderExecReport {
    std::string symbol;
    std::string clOrdID;
    std::string orderID;
    std::string execID;
    char execType = '0';
    char ordStatus = '0';
    char side = '0';       // '1'=Buy, '2'=Sell
    double orderQty = 0.0;
    double cumQty = 0.0;
    double leavesQty = 0.0;
    double avgPx = 0.0;
    double lastPx = 0.0;
    double lastQty = 0.0;
    std::string text;
    uint64_t timestamp = 0;
    
    bool isFill() const { return execType == ExecType::Fill || execType == ExecType::PartialFill; }
    bool isNew() const { return execType == ExecType::New; }
    bool isReject() const { return execType == ExecType::Rejected; }
    bool isCancel() const { return execType == ExecType::Canceled; }
};

// =============================================================================
// CALLBACKS
// =============================================================================
using CTraderTickCallback = std::function<void(const CTraderTick&)>;
using CTraderExecCallback = std::function<void(const CTraderExecReport&)>;
using CTraderStateCallback = std::function<void(bool connected)>;
using CTraderLatencyCallback = std::function<void(const std::string& symbol, double rtt_ms, double slippage_bps)>;

// =============================================================================
// CONFIGURATION
// =============================================================================
struct OpenAPIConfig {
    std::string clientId;
    std::string clientSecret;
    std::string accessToken;
    std::string refreshToken;
    uint64_t accountId = 0;
    std::string host = "demo.ctraderapi.com";
    int port = 5035;
    bool isLive = false;
};

} // namespace Chimera
