# Chimera v4.9.22 - CRITICAL FIX: Response Routing

**Date:** 2026-01-03  
**Status:** Production Ready

## Root Cause Analysis

The signing was CORRECT since v4.9.21. Orders were being ACCEPTED by Binance.
The issue was that **responses were being dropped silently**:

1. **Socket was BLOCKING** - `SSL_read()` waited forever, `poll()` never returned
2. **Response matching used `symbol_id`** instead of Binance WS `"id"` field

## Critical Fixes

### 1. Non-Blocking Socket Mode (BinanceWebSocket.hpp)

After WebSocket handshake completes, socket is now explicitly set to non-blocking:

```cpp
// Windows
ioctlsocket(socket_, FIONBIO, &iMode);

// Linux  
fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
```

This allows `SSL_read()` to return immediately with `SSL_ERROR_WANT_READ` when no data is available.

### 2. Request ID Tracking (BinanceHMAC.hpp)

`WSAPIRequestBuilder` now:
- Uses atomic global `request_id` counter
- Exposes `last_request_id()` for response matching
- Logs request ID with each order sent

### 3. Response Routing by ID (BinanceOrderSender.hpp)

All responses now matched by Binance WS `"id"` field:

```cpp
// OLD (broken): Match by symbol
if (probe.symbol_id == resp.symbol_id && !probe.acked)

// NEW (correct): Match by request ID
auto probe_it = pending_probes_.find(resp.request_id);
```

### 4. Enhanced Logging

- Full response payload logged when received
- Parsed fields (request_id, http_status, order_id) displayed
- Visual banners for ACK/REJECT events
- Request ID logged when probes are sent

## Expected Behavior After Fix

Within 1-3 seconds of startup:
```
[WS-RX] RECEIVED BINANCE RESPONSE (xxx bytes)
[BINANCE_RESP] Parsed: request_id=1 http_status=200 ...
[PROBE_ACK] ✓ RESPONSE MATCHED!
   Request ID: 1
   Exchange Order ID: 123456789
   Latency: 42.123ms
   Total samples: 1
```

Bootstrap will progress:
```
[BOOTSTRAP-BTCUSDT] probes=1/30
[BOOTSTRAP-BTCUSDT] CONFIDENCE REACHED
[LATENCY] p50=42ms p95=63ms
[EXECUTION] latency gate OPEN
```

## Files Modified

1. **crypto_engine/include/binance/BinanceWebSocket.hpp**
   - Added non-blocking socket mode after handshake

2. **crypto_engine/include/binance/BinanceHMAC.hpp**
   - Added `last_request_id_` member
   - Added `last_request_id()` accessor
   - Request ID now atomic and tracked

3. **crypto_engine/include/binance/BinanceOrderSender.hpp**
   - `OrderResponse` struct: Added `request_id`, `http_status`
   - `ProbeOrder` struct: Added `request_id` field
   - `pending_probes_`: Now keyed by `request_id` (not `client_order_id`)
   - `parse_response()`: Extracts outer `"id"` and `"status"` fields
   - `handle_response()`: Matches by `request_id`, enhanced logging
   - Error handling: Checks probe rejection by `request_id`

## What Was NOT Changed

Do NOT touch these - they are CORRECT:
- ✅ HMAC-SHA256 signing
- ✅ Canonical parameter ordering
- ✅ `apiKey` in signature
- ✅ `newOrderRespType=ACK`
- ✅ Monotonic timestamps
- ✅ Precision handling

## Deployment

1. Cross-compile with Zig on Mac for Windows target
2. Copy to VPS via RDP to `C:\Chimera\`
3. In WSL: `cp /mnt/c/Chimera/*.zip ~/ && unzip -o *.zip`
4. Archive old: `mv ~/Chimera ~/Chimera_archive_$(date +%Y%m%d_%H%M%S)`
5. Move new: `mv ~/Chimera_v4.9.22 ~/Chimera`
6. Build: `cd ~/Chimera/build && cmake .. && make -j4`
7. Run: `./chimera`
