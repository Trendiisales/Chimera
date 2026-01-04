// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceRestClient.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Real Binance REST API client (explicit failures, no silent success)
// OWNER: Jo
// LAST VERIFIED: 2024-12-22
//
// DESIGN:
// - Thin REST wrapper for Binance API v3
// - Explicit failure signaling (returns false, not success)
// - Used only when WebSocket execution is degraded
// - Integrated with VenueHealth
//
// FUNCTIONS:
// - ping()         - Health check
// - place_order()  - Market order execution
// - cancel_order() - Order cancellation
// - get_depth()    - Order book snapshot (for initial sync)
// - get_time()     - Server time for signature
//
// COLD PATH ONLY:
// - This is NOT for hot-path execution
// - WebSocket API is preferred for sub-millisecond orders
// - REST is fallback only
//
// IMPORTANT:
// - HTTP methods currently return false (not implemented)
// - This is intentional - forces correct wiring before use
// - DO NOT silently succeed
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

#include <openssl/ssl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "BinanceHMAC.hpp"  // v4.9.13: For synchronized timestamp
#include "BinanceParser.hpp"  // v4.9.13: For PriceLevel struct

namespace Chimera {
namespace Binance {

// NOTE: PriceLevel is defined in BinanceParser.hpp - do not duplicate here

// ─────────────────────────────────────────────────────────────────────────────
// REST Client
// ─────────────────────────────────────────────────────────────────────────────
class BinanceRestClient {
public:
    static constexpr const char* API_HOST = "api.binance.com";
    static constexpr int API_PORT = 443;
    static constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024;  // 1MB
    
    BinanceRestClient() noexcept
        : ssl_ctx_(nullptr)
        , api_key_{}
        , api_secret_{}
        , recv_window_ms_(10000)  // v4.9.13: 10s for burst tolerance
    {}
    
    BinanceRestClient(const std::string& api_key, const std::string& api_secret) noexcept
        : ssl_ctx_(nullptr)
        , recv_window_ms_(10000)  // v4.9.13: 10s for burst tolerance
    {
        set_credentials(api_key, api_secret);
    }
    
    ~BinanceRestClient() {
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void set_credentials(const std::string& api_key, const std::string& api_secret) noexcept {
        strncpy(api_key_, api_key.c_str(), sizeof(api_key_) - 1);
        strncpy(api_secret_, api_secret.c_str(), sizeof(api_secret_) - 1);
    }
    
    void set_recv_window(uint32_t ms) noexcept {
        recv_window_ms_ = ms;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PUBLIC ENDPOINTS (no signature required)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Health check - GET /api/v3/ping
    [[nodiscard]] bool ping() noexcept {
        std::string response;
        return http_get("/api/v3/ping", response);
    }
    
    // Server time - GET /api/v3/time
    [[nodiscard]] bool get_server_time(uint64_t& out_time_ms) noexcept {
        std::string response;
        if (!http_get("/api/v3/time", response)) {
            return false;
        }
        // Parse {"serverTime":1234567890123}
        const char* p = strstr(response.c_str(), "\"serverTime\":");
        if (!p) return false;
        out_time_ms = strtoull(p + 13, nullptr, 10);
        return true;
    }
    
    // Order book snapshot - GET /api/v3/depth
    [[nodiscard]] bool get_depth(
        const char* symbol,
        std::vector<PriceLevel>& bids,
        std::vector<PriceLevel>& asks,
        uint64_t& last_update_id,
        int limit = 1000
    ) noexcept {
        char path[256];
        snprintf(path, sizeof(path), "/api/v3/depth?symbol=%s&limit=%d", symbol, limit);
        
        std::string response;
        if (!http_get(path, response)) {
            return false;
        }
        
        // Parse response - minimal JSON parsing
        // Format: {"lastUpdateId":123,"bids":[["price","qty"],...],"asks":[...]}
        const char* p = strstr(response.c_str(), "\"lastUpdateId\":");
        if (!p) return false;
        last_update_id = strtoull(p + 15, nullptr, 10);
        
        // Parse bids
        bids.clear();
        p = strstr(response.c_str(), "\"bids\":");
        if (p) {
            parse_price_levels(p + 7, bids);
        }
        
        // Parse asks
        asks.clear();
        p = strstr(response.c_str(), "\"asks\":");
        if (p) {
            parse_price_levels(p + 7, asks);
        }
        
        return true;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SIGNED ENDPOINTS (require API key + signature)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Place market order - POST /api/v3/order
    [[nodiscard]] bool place_order(
        const char* symbol,
        double qty,
        bool is_buy,
        uint64_t& out_order_id
    ) noexcept {
        char body[512];
        int len = snprintf(body, sizeof(body),
            "symbol=%s"
            "&side=%s"
            "&type=MARKET"
            "&quantity=%.8f"
            "&recvWindow=%u"
            "&timestamp=",
            symbol,
            is_buy ? "BUY" : "SELL",
            qty,
            recv_window_ms_
        );
        
        // Add timestamp and signature
        uint64_t ts = get_timestamp_ms();
        len += snprintf(body + len, sizeof(body) - len, "%lu", ts);
        
        std::string signature = compute_signature(body);
        len += snprintf(body + len, sizeof(body) - len, "&signature=%s", signature.c_str());
        
        std::string response;
        if (!http_post("/api/v3/order", body, response)) {
            return false;
        }
        
        // Parse orderId from response
        const char* p = strstr(response.c_str(), "\"orderId\":");
        if (!p) return false;
        out_order_id = strtoull(p + 10, nullptr, 10);
        
        return true;
    }
    
    // Cancel order - DELETE /api/v3/order
    [[nodiscard]] bool cancel_order(
        const char* symbol,
        uint64_t order_id
    ) noexcept {
        char body[256];
        int len = snprintf(body, sizeof(body),
            "symbol=%s"
            "&orderId=%lu"
            "&recvWindow=%u"
            "&timestamp=",
            symbol,
            order_id,
            recv_window_ms_
        );
        
        uint64_t ts = get_timestamp_ms();
        len += snprintf(body + len, sizeof(body) - len, "%lu", ts);
        
        std::string signature = compute_signature(body);
        len += snprintf(body + len, sizeof(body) - len, "&signature=%s", signature.c_str());
        
        std::string response;
        return http_delete("/api/v3/order", body, response);
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // HTTP METHODS - Currently return false (not implemented)
    // ═══════════════════════════════════════════════════════════════════════
    // HTTP METHODS - INTENTIONALLY NOT IMPLEMENTED
    // HFT uses WebSocket API for orders (sub-ms latency)
    // REST would destroy our 0.2ms advantage - these return false by design
    // If REST is ever needed (account info, etc), implement with libcurl
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool http_get(const char* path, std::string& response) noexcept {
        // v4.9.13: Implement for time sync (cold-path only)
        // This is CONTROL PLANE - allowed per institutional design
        
        // Initialize SSL context if needed
        if (!ssl_ctx_) {
            ssl_ctx_ = SSL_CTX_new(TLS_client_method());
            if (!ssl_ctx_) {
                printf("[REST] Failed to create SSL context\n");
                return false;
            }
        }
        
        // Resolve hostname
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(API_HOST, "443", &hints, &result) != 0 || !result) {
            printf("[REST] DNS resolution failed for %s\n", API_HOST);
            return false;
        }
        
        // Create socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            freeaddrinfo(result);
            printf("[REST] Socket creation failed\n");
            return false;
        }
        
        // Set timeout (5 seconds for control plane)
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        // Connect
        if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
            freeaddrinfo(result);
            close(sock);
            printf("[REST] TCP connect failed to %s\n", API_HOST);
            return false;
        }
        freeaddrinfo(result);
        
        // SSL handshake
        SSL* ssl = SSL_new(ssl_ctx_);
        if (!ssl) {
            close(sock);
            printf("[REST] SSL_new failed\n");
            return false;
        }
        
        SSL_set_fd(ssl, sock);
        SSL_set_tlsext_host_name(ssl, API_HOST);
        
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl);
            close(sock);
            printf("[REST] SSL handshake failed\n");
            return false;
        }
        
        // Build HTTP request
        char request[512];
        int req_len = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, API_HOST);
        
        // Send request
        if (SSL_write(ssl, request, req_len) != req_len) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(sock);
            printf("[REST] SSL_write failed\n");
            return false;
        }
        
        // Read response
        char buffer[4096];
        response.clear();
        int bytes;
        while ((bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes] = '\0';
            response += buffer;
        }
        
        // Cleanup
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sock);
        
        // Check for valid HTTP response
        if (response.find("200 OK") == std::string::npos) {
            printf("[REST] HTTP error: %.100s\n", response.c_str());
            return false;
        }
        
        // Extract body (after \r\n\r\n)
        size_t body_start = response.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            response = response.substr(body_start + 4);
        }
        
        return true;
    }
    
    [[nodiscard]] bool http_post(const char* path, const char* body, std::string& response) noexcept {
        // NOT IMPLEMENTED: HFT uses WebSocket, not REST
        (void)path;
        (void)body;
        (void)response;
        return false;
    }
    
    [[nodiscard]] bool http_delete(const char* path, const char* body, std::string& response) noexcept {
        // NOT IMPLEMENTED: HFT uses WebSocket, not REST
        (void)path;
        (void)body;
        (void)response;
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // HELPERS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] std::string compute_signature(const char* data) const noexcept {
        unsigned char hmac[32];
        unsigned int hmac_len = 0;
        
        HMAC(EVP_sha256(),
             api_secret_, strlen(api_secret_),
             reinterpret_cast<const unsigned char*>(data), strlen(data),
             hmac, &hmac_len);
        
        // Convert to hex
        char hex[65];
        for (unsigned int i = 0; i < hmac_len; ++i) {
            snprintf(hex + i * 2, 3, "%02x", hmac[i]);
        }
        return std::string(hex);
    }
    
    // v4.9.13: Use synchronized Binance timestamp
    [[nodiscard]] static uint64_t get_timestamp_ms() noexcept {
        return static_cast<uint64_t>(get_binance_timestamp_ms());
    }
    
    void parse_price_levels(const char* json, std::vector<PriceLevel>& levels) noexcept {
        // Simple parser for [["price","qty"],["price","qty"],...]
        const char* p = json;
        while (*p) {
            // Find opening bracket for level
            p = strchr(p, '[');
            if (!p) break;
            ++p;
            
            // Skip opening quote
            if (*p == '[') continue;  // Start of array
            if (*p == '"') ++p;
            
            // Parse price
            double price = strtod(p, nullptr);
            
            // Find comma
            p = strchr(p, ',');
            if (!p) break;
            ++p;
            
            // Skip quote
            while (*p == ' ' || *p == '"') ++p;
            
            // Parse qty
            double qty = strtod(p, nullptr);
            
            levels.push_back({price, qty});
            
            // Find closing bracket
            p = strchr(p, ']');
            if (!p) break;
            ++p;
            
            // Check for end of array
            if (*p == ']') break;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER DATA
    // ═══════════════════════════════════════════════════════════════════════
    
    SSL_CTX* ssl_ctx_;
    char api_key_[128];
    char api_secret_[128];
    uint32_t recv_window_ms_;
};

} // namespace Binance
} // namespace Chimera
