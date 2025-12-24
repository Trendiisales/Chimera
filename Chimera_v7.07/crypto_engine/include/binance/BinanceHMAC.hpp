// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceHMAC.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: HMAC-SHA256 signing for Binance WebSocket API authentication
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DESIGN:
// - Uses OpenSSL for HMAC-SHA256
// - Pre-allocates buffers to avoid heap allocation on hot path
// - Signature is hex-encoded (64 chars for SHA256)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// HMAC-SHA256 Signer
// ─────────────────────────────────────────────────────────────────────────────
// Thread-safe: Each thread should have its own instance.
// ─────────────────────────────────────────────────────────────────────────────
class HMACSigner {
public:
    // Initialize with secret key
    explicit HMACSigner(const char* secret_key) noexcept {
        // v6.95: Handle nullptr for production mode (market data only, no trading)
        if (secret_key == nullptr) {
            secret_len_ = 0;
            secret_[0] = '\0';
            return;
        }
        secret_len_ = strlen(secret_key);
        if (secret_len_ > sizeof(secret_) - 1) {
            secret_len_ = sizeof(secret_) - 1;
        }
        memcpy(secret_, secret_key, secret_len_);
        secret_[secret_len_] = '\0';
    }
    
    // Sign a message and write hex signature to output buffer
    // Returns number of bytes written (64 for SHA256 hex) or 0 on error
    [[nodiscard]] size_t sign(
        const char* message, 
        size_t message_len,
        char* signature_out,
        size_t signature_out_size
    ) const noexcept {
        if (signature_out_size < 65) return 0;  // Need 64 hex chars + null
        
        unsigned char digest[32];  // SHA256 = 32 bytes
        unsigned int digest_len = 0;
        
        // Compute HMAC-SHA256
        unsigned char* result = HMAC(
            EVP_sha256(),
            secret_, secret_len_,
            reinterpret_cast<const unsigned char*>(message), message_len,
            digest, &digest_len
        );
        
        if (!result || digest_len != 32) return 0;
        
        // Convert to hex
        static constexpr char hex_chars[] = "0123456789abcdef";
        for (unsigned int i = 0; i < digest_len; ++i) {
            signature_out[i * 2]     = hex_chars[(digest[i] >> 4) & 0xF];
            signature_out[i * 2 + 1] = hex_chars[digest[i] & 0xF];
        }
        signature_out[64] = '\0';
        
        return 64;
    }
    
    // Convenience overload for null-terminated strings
    [[nodiscard]] size_t sign(
        const char* message,
        char* signature_out,
        size_t signature_out_size
    ) const noexcept {
        return sign(message, strlen(message), signature_out, signature_out_size);
    }

private:
    char   secret_[128];
    size_t secret_len_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Timestamp Helper
// ─────────────────────────────────────────────────────────────────────────────
// Binance requires timestamp in milliseconds since epoch

[[nodiscard]] inline uint64_t get_timestamp_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

// Write timestamp to buffer, return chars written
inline int write_timestamp(char* buf, size_t buf_size) noexcept {
    return snprintf(buf, buf_size, "%llu", static_cast<unsigned long long>(get_timestamp_ms()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Query String Builder
// ─────────────────────────────────────────────────────────────────────────────
// Builds signed query strings for WebSocket API requests

class QueryBuilder {
public:
    QueryBuilder() noexcept : len_(0) {
        buf_[0] = '\0';
    }
    
    // Add a string parameter
    QueryBuilder& add(const char* key, const char* value) noexcept {
        if (len_ > 0) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        }
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%s", key, value);
        return *this;
    }
    
    // Add an integer parameter
    QueryBuilder& add(const char* key, int64_t value) noexcept {
        if (len_ > 0) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        }
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%lld", key, static_cast<long long>(value));
        return *this;
    }
    
    // Add a double parameter with precision
    QueryBuilder& add(const char* key, double value, int precision) noexcept {
        if (len_ > 0) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        }
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%.*f", key, precision, value);
        return *this;
    }
    
    // Add timestamp
    QueryBuilder& add_timestamp() noexcept {
        return add("timestamp", static_cast<int64_t>(get_timestamp_ms()));
    }
    
    // Add signature using signer
    QueryBuilder& add_signature(const HMACSigner& signer) noexcept {
        char sig[65];
        if (signer.sign(buf_, len_, sig, sizeof(sig)) == 64) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&signature=%s", sig);
        }
        return *this;
    }
    
    // Get the built query string
    [[nodiscard]] const char* str() const noexcept { return buf_; }
    [[nodiscard]] size_t length() const noexcept { return len_; }
    
    // Reset for reuse
    void reset() noexcept {
        len_ = 0;
        buf_[0] = '\0';
    }

private:
    char   buf_[2048];
    size_t len_;
};

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket API Request Builder
// ─────────────────────────────────────────────────────────────────────────────
// Builds JSON requests for Binance WebSocket API

class WSAPIRequestBuilder {
public:
    WSAPIRequestBuilder() noexcept : len_(0) {
        buf_[0] = '\0';
    }
    
    // Build a new order request
    // Returns pointer to internal buffer (valid until next build call)
    [[nodiscard]] const char* build_new_order(
        const char* symbol,
        const char* side,         // "BUY" or "SELL"
        const char* type,         // "LIMIT", "MARKET"
        double quantity,
        int qty_precision,
        double price,             // ignored for MARKET
        int price_precision,
        const char* time_in_force, // "GTC", "IOC", "FOK"
        const char* client_order_id,
        const HMACSigner& signer,
        const char* api_key
    ) noexcept {
        // Build params for signing
        QueryBuilder params;
        params.add("symbol", symbol)
              .add("side", side)
              .add("type", type)
              .add("quantity", quantity, qty_precision);
        
        if (strcmp(type, "LIMIT") == 0) {
            params.add("price", price, price_precision)
                  .add("timeInForce", time_in_force);
        }
        
        if (client_order_id && client_order_id[0]) {
            params.add("newClientOrderId", client_order_id);
        }
        
        params.add_timestamp()
              .add_signature(signer);
        
        // Build JSON request
        // Note: We use a static request ID counter
        static uint64_t request_id = 1;
        
        len_ = snprintf(buf_, sizeof(buf_),
            R"({"id":"%llu","method":"order.place","params":{)"
            R"("apiKey":"%s",)"
            R"("symbol":"%s",)"
            R"("side":"%s",)"
            R"("type":"%s",)"
            R"("quantity":"%.*f",)",
            static_cast<unsigned long long>(request_id++),
            api_key,
            symbol,
            side,
            type,
            qty_precision, quantity
        );
        
        if (strcmp(type, "LIMIT") == 0) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_,
                R"("price":"%.*f",)"
                R"("timeInForce":"%s",)",
                price_precision, price,
                time_in_force
            );
        }
        
        if (client_order_id && client_order_id[0]) {
            len_ += snprintf(buf_ + len_, sizeof(buf_) - len_,
                R"("newClientOrderId":"%s",)",
                client_order_id
            );
        }
        
        // Add timestamp and signature
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_,
            R"("timestamp":%llu,)"
            R"("signature":"%s"}})",
            static_cast<unsigned long long>(get_timestamp_ms()),
            extract_signature(params.str())
        );
        
        return buf_;
    }
    
    // Build cancel order request
    [[nodiscard]] const char* build_cancel_order(
        const char* symbol,
        int64_t order_id,
        const HMACSigner& signer,
        const char* api_key
    ) noexcept {
        QueryBuilder params;
        params.add("symbol", symbol)
              .add("orderId", order_id)
              .add_timestamp()
              .add_signature(signer);
        
        static uint64_t request_id = 1000000;
        
        len_ = snprintf(buf_, sizeof(buf_),
            R"({"id":"%llu","method":"order.cancel","params":{)"
            R"("apiKey":"%s",)"
            R"("symbol":"%s",)"
            R"("orderId":%lld,)"
            R"("timestamp":%llu,)"
            R"("signature":"%s"}})",
            static_cast<unsigned long long>(request_id++),
            api_key,
            symbol,
            static_cast<long long>(order_id),
            static_cast<unsigned long long>(get_timestamp_ms()),
            extract_signature(params.str())
        );
        
        return buf_;
    }
    
    [[nodiscard]] const char* str() const noexcept { return buf_; }
    [[nodiscard]] size_t length() const noexcept { return len_; }

private:
    char   buf_[4096];
    size_t len_;
    
    // Extract signature from query string (finds &signature= and returns the value)
    static const char* extract_signature(const char* query) noexcept {
        const char* sig = strstr(query, "&signature=");
        if (sig) {
            return sig + 11;  // Skip "&signature="
        }
        return "";
    }
};

} // namespace Binance
} // namespace Chimera
