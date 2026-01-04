// ═══════════════════════════════════════════════════════════════════════════════
// test_binance_full.cpp - COMPLETE END-TO-END BINANCE WS API TEST
// ═══════════════════════════════════════════════════════════════════════════════
// Compile: g++ -std=c++17 -o test_binance test_binance_full.cpp -lssl -lcrypto
// Run:     ./test_binance
// ═══════════════════════════════════════════════════════════════════════════════

#include <iostream>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <vector>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define SOCK_CLOSE closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using socket_t = int;
#define SOCK_CLOSE close
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

// ─────────────────────────────────────────────────────────────────────────────
// PRODUCTION CREDENTIALS
// ─────────────────────────────────────────────────────────────────────────────
const char* API_KEY = "J3jwWRSWCPLN4N4vfav0gSze6bWVs6eruZzzAfhmU86u1nDk0ESXEisgzsQujndH";
const char* SECRET  = "7QAqP69rjcBhFEwJq8IWPs4DVVRHCcEuSPkJJ4O4tlIYlVMlE4eNtdQnBmWtt0Nu";

// Binance WebSocket Trading API endpoint
const char* WS_HOST = "ws-api.binance.com";
const int   WS_PORT = 443;
const char* WS_PATH = "/ws-api/v3";

// ─────────────────────────────────────────────────────────────────────────────
// HMAC-SHA256
// ─────────────────────────────────────────────────────────────────────────────
std::string hmac_sha256(const std::string& message, const std::string& secret) {
    unsigned char digest[32];
    unsigned int len = 0;
    HMAC(EVP_sha256(), secret.c_str(), secret.length(),
         (const unsigned char*)message.c_str(), message.length(),
         digest, &len);
    
    static const char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (int i = 0; i < 32; i++) {
        result[i*2]   = hex[(digest[i] >> 4) & 0xF];
        result[i*2+1] = hex[digest[i] & 0xF];
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CANONICAL PARAM HANDLING (same as BinanceHMAC.hpp)
// ─────────────────────────────────────────────────────────────────────────────
struct Param {
    std::string key;
    std::string value;
    bool is_string;  // quote in JSON?
};

std::string build_query(const std::vector<Param>& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) s += "&";
        s += params[i].key + "=" + params[i].value;
    }
    return s;
}

std::string build_json_params(const std::vector<Param>& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) s += ",";
        if (params[i].is_string)
            s += "\"" + params[i].key + "\":\"" + params[i].value + "\"";
        else
            s += "\"" + params[i].key + "\":" + params[i].value;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// FORMAT WITH 8 DECIMALS (critical!)
// ─────────────────────────────────────────────────────────────────────────────
std::string format8(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.8f", v);
    return buf;
}

int main() {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BINANCE WEBSOCKET TRADING API - COMPLETE TEST\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 1: Get current timestamp
    // ═══════════════════════════════════════════════════════════════════════
    int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    printf("1. TIMESTAMP: %lld\n\n", (long long)ts);

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 2: Build params (LIMIT order far from market)
    // ═══════════════════════════════════════════════════════════════════════
    std::vector<Param> params;
    params.push_back({"apiKey", API_KEY, true});
    params.push_back({"newClientOrderId", "CHIMERA_TEST_001", true});
    params.push_back({"price", format8(50000.0), true});      // Far from market
    params.push_back({"quantity", format8(0.0005), true});    // Min qty
    params.push_back({"recvWindow", "10000", false});         // Number
    params.push_back({"side", "BUY", true});
    params.push_back({"symbol", "BTCUSDT", true});
    params.push_back({"timeInForce", "GTC", true});
    params.push_back({"timestamp", std::to_string(ts), false}); // Number
    params.push_back({"type", "LIMIT", true});
    
    // Sort alphabetically
    std::sort(params.begin(), params.end(), [](auto& a, auto& b) {
        return a.key < b.key;
    });
    
    printf("2. PARAMS (sorted alphabetically):\n");
    for (auto& p : params)
        printf("   %-20s = %s\n", p.key.c_str(), p.value.c_str());
    printf("\n");

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 3: Build query string and sign
    // ═══════════════════════════════════════════════════════════════════════
    std::string query = build_query(params);
    std::string sig = hmac_sha256(query, SECRET);
    
    printf("3. QUERY STRING TO SIGN:\n");
    printf("   %s\n\n", query.c_str());
    printf("4. SIGNATURE:\n");
    printf("   %s\n\n", sig.c_str());

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 4: Build full JSON request
    // ═══════════════════════════════════════════════════════════════════════
    std::string json_params = build_json_params(params);
    
    char json[4096];
    snprintf(json, sizeof(json),
        R"({"id":"1","method":"order.place","params":{%s,"signature":"%s"}})",
        json_params.c_str(), sig.c_str());
    
    printf("5. FULL JSON REQUEST:\n");
    printf("   %s\n\n", json);

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 5: Connect to Binance
    // ═══════════════════════════════════════════════════════════════════════
    printf("6. CONNECTING TO %s:%d%s...\n", WS_HOST, WS_PORT, WS_PATH);
    
    struct addrinfo hints = {}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(WS_HOST, "443", &hints, &res) != 0) {
        printf("   ✗ DNS resolution failed!\n");
        return 1;
    }
    
    socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        printf("   ✗ Socket creation failed!\n");
        return 1;
    }
    
    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        printf("   ✗ TCP connect failed!\n");
        return 1;
    }
    freeaddrinfo(res);
    printf("   ✓ TCP connected\n");
    
    // SSL
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, WS_HOST);
    
    if (SSL_connect(ssl) != 1) {
        printf("   ✗ SSL handshake failed!\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    printf("   ✓ SSL connected\n");
    
    // WebSocket upgrade
    char ws_req[1024];
    snprintf(ws_req, sizeof(ws_req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", WS_PATH, WS_HOST);
    
    SSL_write(ssl, ws_req, strlen(ws_req));
    
    char resp[4096];
    int n = SSL_read(ssl, resp, sizeof(resp)-1);
    resp[n] = 0;
    
    if (!strstr(resp, "101")) {
        printf("   ✗ WebSocket upgrade failed!\n");
        printf("   Response: %s\n", resp);
        return 1;
    }
    printf("   ✓ WebSocket connected!\n\n");

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 6: Send order request
    // ═══════════════════════════════════════════════════════════════════════
    printf("7. SENDING ORDER REQUEST...\n");
    
    size_t json_len = strlen(json);
    unsigned char frame[4096];
    size_t flen = 0;
    
    frame[flen++] = 0x81;  // FIN + TEXT
    if (json_len < 126) {
        frame[flen++] = 0x80 | json_len;  // MASK + len
    } else {
        frame[flen++] = 0x80 | 126;
        frame[flen++] = (json_len >> 8) & 0xFF;
        frame[flen++] = json_len & 0xFF;
    }
    
    // Mask key
    uint32_t mask = 0x12345678;
    memcpy(frame + flen, &mask, 4);
    flen += 4;
    
    // Masked payload
    for (size_t i = 0; i < json_len; i++)
        frame[flen++] = json[i] ^ ((mask >> ((i%4)*8)) & 0xFF);
    
    SSL_write(ssl, frame, flen);
    printf("   Sent %zu bytes\n\n", flen);

    // ═══════════════════════════════════════════════════════════════════════
    // STEP 7: Read response
    // ═══════════════════════════════════════════════════════════════════════
    printf("8. WAITING FOR RESPONSE...\n");
    
    unsigned char rbuf[8192];
    n = SSL_read(ssl, rbuf, sizeof(rbuf)-1);
    
    if (n > 0) {
        // Parse WebSocket frame
        int opcode = rbuf[0] & 0x0F;
        size_t plen = rbuf[1] & 0x7F;
        size_t hdr = 2;
        if (plen == 126) {
            plen = (rbuf[2] << 8) | rbuf[3];
            hdr = 4;
        }
        
        char* payload = (char*)(rbuf + hdr);
        payload[plen] = 0;
        
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("  BINANCE RESPONSE\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("%s\n\n", payload);
        
        if (strstr(payload, "\"result\"")) {
            printf("✓ SUCCESS! Order accepted by Binance!\n");
        } else if (strstr(payload, "-1022")) {
            printf("✗ ERROR -1022: Signature invalid\n");
            printf("\n  This means the signing is still wrong.\n");
            printf("  Check that query string matches what Binance expects.\n");
        } else if (strstr(payload, "-1021")) {
            printf("✗ ERROR -1021: Timestamp outside recvWindow\n");
            printf("  Need to sync time with Binance server.\n");
        } else if (strstr(payload, "error")) {
            printf("✗ Some other error - see response above\n");
        }
    } else {
        printf("   No response received (timeout?)\n");
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    SOCK_CLOSE(sock);

    printf("\n═══════════════════════════════════════════════════════════════\n");
    return 0;
}
