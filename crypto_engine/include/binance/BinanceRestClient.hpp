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

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// Order Book Level (for snapshot)
// ─────────────────────────────────────────────────────────────────────────────
struct PriceLevel {
    double price;
    double qty;
};

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
        , recv_window_ms_(5000)
    {}
    
    BinanceRestClient(const std::string& api_key, const std::string& api_secret) noexcept
        : ssl_ctx_(nullptr)
        , recv_window_ms_(5000)
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
    // This is INTENTIONAL - forces correct wiring before use
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool http_get(const char* path, std::string& response) noexcept {
        // TODO: Implement with SSL socket
        // DO NOT SILENTLY SUCCEED
        (void)path;
        (void)response;
        return false;
    }
    
    [[nodiscard]] bool http_post(const char* path, const char* body, std::string& response) noexcept {
        // TODO: Implement with SSL socket
        // DO NOT SILENTLY SUCCEED
        (void)path;
        (void)body;
        (void)response;
        return false;
    }
    
    [[nodiscard]] bool http_delete(const char* path, const char* body, std::string& response) noexcept {
        // TODO: Implement with SSL socket
        // DO NOT SILENTLY SUCCEED
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
    
    [[nodiscard]] static uint64_t get_timestamp_ms() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
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
