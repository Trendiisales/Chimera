# Chimera v4.9.19 - DIAGNOSTIC BUILD

## Purpose
This is a **diagnostic build** to help identify why Binance WebSocket API orders are being rejected with -1022 (signature invalid).

## Changes from v4.9.18

### 1. Enhanced OrderSender Startup Logging
- Shows EXACT endpoint being connected to (host, port, path)
- Shows API key (first 20 + last 4 chars)
- Shows time sync status and offset

### 2. Enhanced Signature Diagnostics
- Prints details for **EVERY order** (not just first)
- Shows all params with their types (string vs number)
- Shows exact query string being signed
- Shows full JSON being sent
- Makes it easy to spot any formatting mismatches

### 3. Standalone Test Tool
- `test_binance_wsapi.cpp` - Complete end-to-end test
- Compile: `g++ -std=c++17 -o test_wsapi test_binance_wsapi.cpp -lssl -lcrypto`
- Run: `./test_wsapi`
- Connects directly to Binance and shows response

## Expected Startup Logs
```
╔══════════════════════════════════════════════════════════════╗
║  ORDER SENDER STARTUP - v4.9.19 DIAGNOSTIC                   ║
╠══════════════════════════════════════════════════════════════╣
║  WebSocket Trading API Endpoint:                             ║
║    Host: ws-api.binance.com
║    Port: 443
║    Path: /ws-api/v3
║  API Key: J3jwWRSWCPLN4N4v...jndH
║  Time Sync: ✓ READY (offset=+52ms)
╚══════════════════════════════════════════════════════════════╝

[ORDER-WS] Connecting to ws-api.binance.com:443/ws-api/v3...
[ORDER-WS] ✓ Connected to Binance WebSocket Trading API
```

## Expected Order Logs
```
[SIG-DIAG] ════════════════ ORDER #1 ════════════════
[SIG-DIAG] v4.9.19 CANONICAL SIGNING
[SIG-DIAG] Params (10 total, sorted alphabetically):
[SIG-DIAG]   apiKey = J3jwWRSWCP... (string)
[SIG-DIAG]   newClientOrderId = PRB1000000 (string)
[SIG-DIAG]   price = 50000.00000000 (string)
[SIG-DIAG]   quantity = 0.00050000 (string)
[SIG-DIAG]   recvWindow = 10000 (number)
[SIG-DIAG]   side = BUY (string)
[SIG-DIAG]   symbol = BTCUSDT (string)
[SIG-DIAG]   timeInForce = GTC (string)
[SIG-DIAG]   timestamp = 1767427000000 (number)
[SIG-DIAG]   type = LIMIT (string)
[SIG-DIAG] Query string (287 bytes):
[SIG-DIAG]   apiKey=...&newClientOrderId=...&price=50000.00000000&...
[SIG-DIAG] Signature (64 chars): abc123...
[SIG-DIAG] Full JSON (xxx bytes):
[SIG-DIAG]   {"id":"1","method":"order.place","params":{...}}
[SIG-DIAG] ════════════════════════════════════════════════
```

## Debugging Steps
1. Build and run with these enhanced logs
2. Capture the full SIG-DIAG output when an order is sent
3. Check:
   - Are params sorted alphabetically?
   - Is price/quantity 8 decimals?
   - Is timestamp a number (no quotes)?
   - Is recvWindow a number (no quotes)?
   - Is apiKey included in the signed query string?

## Running Standalone Test
```bash
# On VPS
cd ~/Chimera_v4.9.19
g++ -std=c++17 -o test_wsapi test_binance_wsapi.cpp -lssl -lcrypto
./test_wsapi

# This will:
# 1. Generate a test order
# 2. Connect to Binance WS API
# 3. Send the order
# 4. Show the response
```
