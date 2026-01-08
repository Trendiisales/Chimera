# Chimera Open API Migration Plan

## Overview

Migrate from FIX 4.4 protocol to cTrader Open API (Protobuf over TCP/SSL).

**Current:** FIX 4.4 → 14ms latency → Demo only (BlackBull won't give live)
**Target:** Open API → ~30-50ms latency → Live trading available

---

## Files to PRESERVE (FIX Implementation)

These files stay intact - we may need FIX again if we get another broker:

```
cfd_engine/include/fix/
├── CTraderFIXClient.hpp    # 46KB - Main FIX client
├── FIXSession.hpp          # 31KB - FIX session management
├── FIXSSLTransport.hpp     # 31KB - SSL transport layer
├── FIXMessage.hpp          # 25KB - FIX message parsing
├── FIXConfig.hpp           # 11KB - FIX configuration
├── FIXFastParse.hpp        # 4KB  - Fast parsing utilities
├── FIXResendRing.hpp       # 3KB  - Message resend buffer
├── FIXFieldView.hpp        # 1KB  - Field view utilities
├── FixDegradedState.hpp    # 1KB  - Degraded state handling
```

**Total FIX code: ~153KB / ~4,500 lines**

---

## New Files to CREATE (Open API Implementation)

```
cfd_engine/include/openapi/
├── CTraderOpenAPIClient.hpp    # Main Open API client (~800 lines)
├── OpenAPITransport.hpp        # TCP/SSL + Protobuf transport (~400 lines)
├── OpenAPIAuth.hpp             # OAuth2 token management (~200 lines)
├── OpenAPIMessages.hpp         # Protobuf message wrappers (~300 lines)
└── proto/                      # Generated protobuf headers
    ├── OpenApiCommonMessages.pb.h
    ├── OpenApiCommonModelMessages.pb.h
    ├── OpenApiMessages.pb.h
    └── OpenApiModelMessages.pb.h
```

---

## Interface Required (Both FIX and Open API Must Implement)

```cpp
// Shared data structures (move to cfd_engine/include/CTraderTypes.hpp)
struct CTraderTick {
    std::string symbol;
    double bid;
    double ask;
    double bidSize;
    double askSize;
    uint64_t timestamp;
    
    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

struct CTraderExecReport {
    std::string symbol;
    std::string clOrdID;
    std::string orderID;
    std::string execID;
    char execType;       // '0'=New, '1'=PartialFill, '2'=Fill, '4'=Canceled, '8'=Rejected
    char ordStatus;
    char side;           // '1'=Buy, '2'=Sell
    double orderQty;
    double cumQty;
    double leavesQty;
    double avgPx;
    double lastPx;
    double lastQty;
    std::string text;
    uint64_t timestamp;
    
    bool isFill() const;
    bool isNew() const;
    bool isReject() const;
    bool isCancel() const;
};

// Callbacks
using CTraderTickCallback = std::function<void(const CTraderTick&)>;
using CTraderExecCallback = std::function<void(const CTraderExecReport&)>;
using CTraderStateCallback = std::function<void(bool connected)>;
using CTraderLatencyCallback = std::function<void(const std::string& symbol, double rtt_ms, double slippage_bps)>;

// Interface that both FIX and OpenAPI clients implement
class ICTraderClient {
public:
    virtual ~ICTraderClient() = default;
    
    // Connection
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    // Configuration
    virtual void setConfig(const CTraderConfig& config) = 0;
    
    // Callbacks
    virtual void setOnTick(CTraderTickCallback cb) = 0;
    virtual void setOnExec(CTraderExecCallback cb) = 0;
    virtual void setOnLatency(CTraderLatencyCallback cb) = 0;
    
    // Market Data
    virtual bool subscribeMarketData(const std::string& symbol) = 0;
    virtual bool requestSecurityList() = 0;
    virtual bool isSecurityListReady() const = 0;
    
    // Trading
    virtual bool sendMarketOrder(const std::string& symbol, char side, double qty) = 0;
    
    // Latency
    virtual double fixRttLastMs() const = 0;
    virtual double fixRttMinMs() const = 0;
    virtual double fixRttMaxMs() const = 0;
    virtual double fixRttAvgMs() const = 0;
    virtual size_t fixRttSamples() const = 0;
};
```

---

## Changes to CfdEngine.hpp

### Option A: Compile-time switch (preprocessor)

```cpp
// At top of CfdEngine.hpp
#ifdef USE_OPENAPI
    #include "openapi/CTraderOpenAPIClient.hpp"
    using CTraderClient = CTraderOpenAPIClient;
#else
    #include "fix/CTraderFIXClient.hpp"
    using CTraderClient = CTraderFIXClient;
#endif

// In class:
CTraderClient client_;  // Instead of CTraderFIXClient fixClient_
```

### Option B: Runtime switch (pointer)

```cpp
#include "ICTraderClient.hpp"
#include "fix/CTraderFIXClient.hpp"
#include "openapi/CTraderOpenAPIClient.hpp"

// In class:
std::unique_ptr<ICTraderClient> client_;

// In constructor:
if (useOpenAPI) {
    client_ = std::make_unique<CTraderOpenAPIClient>();
} else {
    client_ = std::make_unique<CTraderFIXClient>();
}
```

**Recommendation: Option A** - no virtual call overhead, cleaner.

---

## Open API Authentication Flow

```
1. User visits OAuth URL in browser (one time):
   https://id.ctrader.com/my/settings/openapi/grantingaccess/
     ?client_id=20131_7MDjxFmb5lsxkjPrbDv7n3lVm7dyW3Ab3ZOwvAomv4OlkbtZH8
     &redirect_uri=http://localhost
     &scope=trading

2. User logs in, authorizes app

3. Browser redirects to: http://localhost?code=AUTHORIZATION_CODE

4. Exchange code for token (REST):
   GET https://openapi.ctrader.com/apps/token
     ?grant_type=authorization_code
     &code=AUTHORIZATION_CODE
     &redirect_uri=http://localhost
     &client_id=CLIENT_ID
     &client_secret=CLIENT_SECRET

5. Response:
   {
     "accessToken": "...",
     "refreshToken": "...",
     "expiresIn": 2628000  // ~30 days
   }

6. Connect to API:
   - Host: demo.ctraderapi.com (demo) or live.ctraderapi.com (live)
   - Port: 5035
   - Protocol: TCP/SSL + Protobuf

7. Send ProtoOAApplicationAuthReq (client_id, client_secret)
8. Send ProtoOAAccountAuthReq (access_token, account_id)
9. Ready to trade!
```

---

## Open API Message Mapping

| FIX Message | Open API Message | Notes |
|-------------|------------------|-------|
| Logon (35=A) | ProtoOAApplicationAuthReq | App auth |
| - | ProtoOAAccountAuthReq | Account auth (new) |
| Market Data Request (35=V) | ProtoOASubscribeSpotsReq | Subscribe ticks |
| Market Data (35=W) | ProtoOASpotEvent | Tick data |
| New Order Single (35=D) | ProtoOANewOrderReq | Place order |
| Execution Report (35=8) | ProtoOAExecutionEvent | Order fill/reject |
| Security List (35=y) | ProtoOASymbolsListReq | Get symbols |
| Heartbeat (35=0) | ProtoOAHeartbeatEvent | Keep alive |
| Test Request (35=1) | ProtoOAHeartbeatEvent | RTT measurement |

---

## Build Changes

### CMakeLists.txt additions:

```cmake
# Protobuf
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# Generate protobuf headers
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    proto/OpenApiCommonMessages.proto
    proto/OpenApiCommonModelMessages.proto
    proto/OpenApiMessages.proto
    proto/OpenApiModelMessages.proto
)

# Add to target
target_sources(chimera PRIVATE ${PROTO_SRCS})
target_link_libraries(chimera ${Protobuf_LIBRARIES})

# Compile flag
add_definitions(-DUSE_OPENAPI)  # or -DUSE_FIX
```

---

## Configuration (config.ini)

```ini
[ctrader]
# Protocol: FIX or OPENAPI
protocol=OPENAPI

# Open API credentials
client_id=20131_7MDjxFmb5lsxkjPrbDv7n3lVm7dyW3Ab3ZOwvAomv4OlkbtZH8
client_secret=GphKfXFnQr7G60Q95HDgvidJ84yZSWHXpD3aW5ecg01SayjV7A
access_token=<from OAuth flow>
refresh_token=<from OAuth flow>
account_id=<your cTrader account ID>

# Endpoint
host=live.ctraderapi.com
port=5035

# FIX config (preserved for future use)
[fix]
quote_host=h68.p.ctrader.com
quote_port=5201
trade_host=h68.p.ctrader.com  
trade_port=5202
sender_comp_id=...
target_comp_id=cServer
username=...
password=...
```

---

## Implementation Order

1. **Create CTraderTypes.hpp** - Move shared structs out of FIXClient
2. **Create ICTraderClient.hpp** - Abstract interface
3. **Create OpenAPITransport.hpp** - TCP/SSL + Protobuf framing
4. **Create OpenAPIAuth.hpp** - OAuth2 token handling
5. **Create CTraderOpenAPIClient.hpp** - Main client implementing interface
6. **Modify CfdEngine.hpp** - Use compile-time switch
7. **Update CMakeLists.txt** - Add protobuf, compile flags
8. **Test on demo** - Validate against BlackBull demo
9. **Go live** - Switch to live endpoint

---

## Credentials (SAVE SECURELY)

```
Client ID: 20131_7MDjxFmb5lsxkjPrbDv7n3lVm7dyW3Ab3ZOwvAomv4OlkbtZH8
Client Secret: GphKfXFnQr7G60Q95HDgvidJ84yZSWHXpD3aW5ecg01SayjV7A
```

Access token: <pending - need app approval>

---

## Timeline

| Task | Time | Status |
|------|------|--------|
| App registration | Done | ✓ |
| App approval | 1-3 days | Waiting |
| Build Open API client | 1-2 days | Ready to start |
| Integration testing | 1 day | After approval |
| Live trading | Same day | After testing |

---

## Rollback Plan

If Open API has issues:
1. Change `#define USE_OPENAPI` to `#define USE_FIX`
2. Rebuild
3. Back to FIX (demo only until BlackBull approves live)

All FIX code preserved, no changes needed.
