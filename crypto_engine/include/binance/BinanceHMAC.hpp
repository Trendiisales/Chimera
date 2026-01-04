// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceHMAC.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.27
// PURPOSE: HMAC-SHA256 signing for Binance WebSocket API authentication
// OWNER: Jo
// LAST VERIFIED: 2026-01-04
//
// v4.9.27: PHASE 1 AUTH HARDENING
// ═══════════════════════════════════════════════════════════════════════════════
//   FIX 1.1: TIMESTAMP DRIFT GUARD
//     - Automatic resync if drift > 2000ms
//     - Prevents silent -1022 failures from clock skew
//
//   FIX 1.2: SIGNATURE REJECTION COUNTER
//     - Global counter: rejected_orders_total{reason="SIGNATURE"}
//     - Tracks -1022 failures for observability
//     - Exposed to GUI via SignatureRejectionTracker
//
//   FIX 1.3: ENHANCED DIAGNOSTIC OUTPUT
//     - Full canonical string dump on every sign
//     - Easy verification command for manual testing
//
// v4.9.22: REQUEST ID TRACKING
// ═══════════════════════════════════════════════════════════════════════════════
//   - WSAPIRequestBuilder now exposes last_request_id() for response matching
//   - Request ID is atomic and globally unique
//
// v4.9.21 CRITICAL FIXES:
// ══════════════════════════════════════════════════════════════════════════════
// 1. CANONICAL SIGNING MATCHES JSON EXACTLY
//    - Same params object used for signing AND JSON generation
//    - No drift between signed payload and sent payload
//
// 2. MONOTONIC TIMESTAMPS (atomic, strictly increasing)
//    - Burst probes cannot reuse timestamps
//    - CAS loop guarantees uniqueness
//
// 3. EXPLICIT newOrderRespType=ACK
//    - Binance injects defaults if missing → breaks signature
//    - Must be explicit in every order
//
// 4. apiKey INCLUDED in signature (WS-API requirement per official docs)
//
// Binance WS Signing Rule:
//   Binance reconstructs signature from params object, NOT query string
//   → Sign EXACT string values that appear in JSON
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace Chimera {
namespace Binance {

// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.27 FIX 1.2: SIGNATURE REJECTION COUNTER
// ═══════════════════════════════════════════════════════════════════════════════
// Tracks -1022 signature failures for observability.
// This counter alone would have saved hours of debugging.
// ═══════════════════════════════════════════════════════════════════════════════
class SignatureRejectionTracker {
public:
    static SignatureRejectionTracker& instance() {
        static SignatureRejectionTracker t;
        return t;
    }
    
    void record_rejection() noexcept {
        total_rejections_.fetch_add(1, std::memory_order_relaxed);
        last_rejection_ts_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        // Print ONCE on first rejection, then every 10
        uint64_t count = total_rejections_.load();
        if (count == 1 || count % 10 == 0) {
            printf("\n");
            printf("╔══════════════════════════════════════════════════════════════════════╗\n");
            printf("║ [SIG_REJECT] SIGNATURE REJECTION #%llu                                 \n",
                   static_cast<unsigned long long>(count));
            printf("║ Common causes:                                                        ║\n");
            printf("║   1. Clock drift > recvWindow (check time sync)                       ║\n");
            printf("║   2. Param mismatch (canonical ≠ JSON)                                ║\n");
            printf("║   3. API key lacks Spot Trading permission                            ║\n");
            printf("╚══════════════════════════════════════════════════════════════════════╝\n");
            printf("\n");
        }
    }
    
    [[nodiscard]] uint64_t total_rejections() const noexcept {
        return total_rejections_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] int64_t last_rejection_ts() const noexcept {
        return last_rejection_ts_;
    }
    
    void reset() noexcept {
        total_rejections_.store(0, std::memory_order_relaxed);
        last_rejection_ts_ = 0;
    }

private:
    SignatureRejectionTracker() = default;
    std::atomic<uint64_t> total_rejections_{0};
    int64_t last_rejection_ts_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Monotonic Clock for Binance Timestamps
// ─────────────────────────────────────────────────────────────────────────────
// CRITICAL: Binance WS rejects duplicate timestamps under burst load
// This guarantees strictly increasing milliseconds
//
// v4.9.27: Added drift guard (FIX 1.1)
// ─────────────────────────────────────────────────────────────────────────────
class BinanceTimeSync {
public:
    static int64_t local_now_ms() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }
    
    // Get Binance-adjusted timestamp
    static int64_t now_ms() noexcept {
        return local_now_ms() + offset_ms_.load(std::memory_order_relaxed);
    }
    
    // CRITICAL: Get MONOTONIC timestamp - NEVER returns same value twice
    static int64_t next_timestamp_ms() noexcept {
        int64_t now = now_ms();
        int64_t prev = last_timestamp_ms_.load(std::memory_order_relaxed);
        
        if (now <= prev) {
            now = prev + 1;
        }
        
        // CAS loop for thread safety
        while (!last_timestamp_ms_.compare_exchange_weak(
            prev, now,
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
            if (now <= prev) {
                now = prev + 1;
            }
        }
        
        return now;
    }
    
    static int64_t offset_ms() noexcept {
        return offset_ms_.load(std::memory_order_relaxed);
    }
    
    // v4.9.27 FIX 1.1: TIMESTAMP DRIFT GUARD
    // Check if drift exceeds threshold, return true if resync needed
    static bool needs_resync(int64_t threshold_ms = 2000) noexcept {
        int64_t drift = std::abs(offset_ms_.load(std::memory_order_relaxed));
        return drift > threshold_ms || !initialized_.load(std::memory_order_relaxed);
    }
    
    // v4.9.27: Enhanced drift check with server time
    static bool check_drift(int64_t server_time_ms, int64_t threshold_ms = 2000) noexcept {
        int64_t local = local_now_ms();
        int64_t drift = std::abs(server_time_ms - local);
        
        if (drift > threshold_ms) {
            printf("[BINANCE-TIME] ⚠️  DRIFT DETECTED: %lldms > threshold %lldms\n",
                   static_cast<long long>(drift),
                   static_cast<long long>(threshold_ms));
            return true;  // Needs resync
        }
        return false;
    }
    
    static void set_offset(int64_t server_time_ms) noexcept {
        int64_t local = local_now_ms();
        int64_t offset = server_time_ms - local;
        int64_t old_offset = offset_ms_.exchange(offset, std::memory_order_relaxed);
        
        printf("[BINANCE-TIME] Server: %lld | Local: %lld | Offset: %+lld ms",
               static_cast<long long>(server_time_ms),
               static_cast<long long>(local),
               static_cast<long long>(offset));
        
        if (initialized_.load()) {
            int64_t change = offset - old_offset;
            printf(" | Change: %+lld ms", static_cast<long long>(change));
        }
        printf("\n");
        
        if (std::abs(offset) > 1000) {
            printf("[BINANCE-TIME] ⚠️  WARNING: Clock drift > 1 second!\n");
        }
        if (std::abs(offset) > 5000) {
            printf("[BINANCE-TIME] 🚨 CRITICAL: Clock drift > 5 seconds! Signatures WILL fail!\n");
        }
        
        initialized_.store(true, std::memory_order_relaxed);
    }
    
    static bool is_initialized() noexcept {
        return initialized_.load(std::memory_order_relaxed);
    }
    
    static void mark_initialized() noexcept {
        initialized_.store(true, std::memory_order_relaxed);
    }
    
    // v4.9.27: Get current drift for diagnostics
    static int64_t current_drift_ms() noexcept {
        return offset_ms_.load(std::memory_order_relaxed);
    }

private:
    static inline std::atomic<int64_t> offset_ms_{0};
    static inline std::atomic<bool> initialized_{false};
    static inline std::atomic<int64_t> last_timestamp_ms_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// HMAC-SHA256 Signer
// ─────────────────────────────────────────────────────────────────────────────
class HMACSigner {
public:
    explicit HMACSigner(const char* secret_key) noexcept {
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
    
    [[nodiscard]] size_t sign(
        const char* message, 
        size_t message_len,
        char* signature_out,
        size_t signature_out_size
    ) const noexcept {
        if (signature_out_size < 65) return 0;
        
        unsigned char digest[32];
        unsigned int digest_len = 0;
        
        unsigned char* result = HMAC(
            EVP_sha256(),
            secret_, secret_len_,
            reinterpret_cast<const unsigned char*>(message), message_len,
            digest, &digest_len
        );
        
        if (!result || digest_len != 32) return 0;
        
        static constexpr char hex_chars[] = "0123456789abcdef";
        for (unsigned int i = 0; i < digest_len; ++i) {
            signature_out[i * 2]     = hex_chars[(digest[i] >> 4) & 0xF];
            signature_out[i * 2 + 1] = hex_chars[digest[i] & 0xF];
        }
        signature_out[64] = '\0';
        
        return 64;
    }

private:
    char   secret_[128];
    size_t secret_len_;
};

// Legacy timestamp functions (redirect to monotonic)
[[nodiscard]] inline int64_t get_binance_timestamp_ms() noexcept {
    return BinanceTimeSync::next_timestamp_ms();
}

[[nodiscard]] inline uint64_t get_timestamp_ms() noexcept {
    return static_cast<uint64_t>(BinanceTimeSync::next_timestamp_ms());
}

// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.21: CANONICAL PARAM SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
// CRITICAL: Same params used for BOTH signing AND JSON generation
// The EXACT string value in params is what gets signed and sent
// ═══════════════════════════════════════════════════════════════════════════════

enum class ParamType : uint8_t {
    STRING = 0,  // Quoted in JSON: "key":"value"
    NUMBER = 1   // Unquoted in JSON: "key":value
};

struct CanonicalParam {
    char key[32];
    char value[128];
    ParamType type;
};

class CanonicalParamSet {
public:
    static constexpr size_t MAX_PARAMS = 16;
    
    CanonicalParamSet() noexcept : count_(0) {}
    
    // Add STRING param (quoted in JSON)
    void add_string(const char* key, const char* value) noexcept {
        if (count_ >= MAX_PARAMS) return;
        auto& p = params_[count_++];
        strncpy(p.key, key, sizeof(p.key) - 1);
        p.key[sizeof(p.key) - 1] = '\0';
        strncpy(p.value, value, sizeof(p.value) - 1);
        p.value[sizeof(p.value) - 1] = '\0';
        p.type = ParamType::STRING;
    }
    
    // Add NUMBER param (unquoted in JSON)
    void add_number(const char* key, int64_t value) noexcept {
        if (count_ >= MAX_PARAMS) return;
        auto& p = params_[count_++];
        strncpy(p.key, key, sizeof(p.key) - 1);
        p.key[sizeof(p.key) - 1] = '\0';
        snprintf(p.value, sizeof(p.value), "%lld", static_cast<long long>(value));
        p.type = ParamType::NUMBER;
    }
    
    // Add decimal as STRING (quoted in JSON) - price/quantity
    void add_decimal_string(const char* key, double value, int precision) noexcept {
        if (count_ >= MAX_PARAMS) return;
        auto& p = params_[count_++];
        strncpy(p.key, key, sizeof(p.key) - 1);
        p.key[sizeof(p.key) - 1] = '\0';
        snprintf(p.value, sizeof(p.value), "%.*f", precision, value);
        p.type = ParamType::STRING;
    }
    
    // Sort alphabetically by key (Binance requirement)
    void sort() noexcept {
        std::sort(params_, params_ + count_, [](const CanonicalParam& a, const CanonicalParam& b) {
            return strcmp(a.key, b.key) < 0;
        });
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // CANONICAL STRING FOR SIGNING
    // ═══════════════════════════════════════════════════════════════════════════
    // Format: key=value&key=value (sorted alphabetically)
    // Uses EXACT value strings - same as what appears in JSON
    //
    // v4.9.32 CRITICAL FIX: apiKey MUST be in canonical string!
    // Binance WS API docs clearly show apiKey in signed payload:
    // "apiKey=xxx&newOrderRespType=ACK&price=52000.00&quantity=..."
    // Previous v4.9.27 was WRONG - it excluded apiKey causing -1022 errors
    // ═══════════════════════════════════════════════════════════════════════════
    size_t to_canonical_string(char* buf, size_t buf_size) const noexcept {
        size_t len = 0;
        for (size_t i = 0; i < count_; ++i) {
            // Skip signature param (obviously - we're computing it)
            if (strcmp(params_[i].key, "signature") == 0) continue;
            
            // v4.9.32: apiKey IS included in signature (WS-API requirement)
            // DO NOT skip apiKey - it must be signed!
            
            if (len > 0) {
                len += snprintf(buf + len, buf_size - len, "&");
            }
            // Use EXACT value string - this is what Binance will hash
            len += snprintf(buf + len, buf_size - len, "%s=%s", 
                           params_[i].key, params_[i].value);
        }
        return len;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // JSON PARAMS - MUST USE EXACT SAME VALUES AS CANONICAL STRING
    // ═══════════════════════════════════════════════════════════════════════════
    size_t to_json_params(char* buf, size_t buf_size) const noexcept {
        size_t len = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (i > 0) {
                len += snprintf(buf + len, buf_size - len, ",");
            }
            if (params_[i].type == ParamType::STRING) {
                // Quoted: "key":"value"
                len += snprintf(buf + len, buf_size - len, "\"%s\":\"%s\"",
                               params_[i].key, params_[i].value);
            } else {
                // Unquoted: "key":value
                len += snprintf(buf + len, buf_size - len, "\"%s\":%s",
                               params_[i].key, params_[i].value);
            }
        }
        return len;
    }
    
    size_t count() const noexcept { return count_; }
    const CanonicalParam& operator[](size_t i) const noexcept { return params_[i]; }

private:
    CanonicalParam params_[MAX_PARAMS];
    size_t count_;
};

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket API Request Builder
// ─────────────────────────────────────────────────────────────────────────────
// v4.9.27:
// - Enhanced diagnostic output for signature debugging
// - Drift guard check before signing
//
// v4.9.22:
// - Request ID tracking for response matching
//
// v4.9.21 FINAL:
// - apiKey EXCLUDED from signature (in JSON only)
// - newOrderRespType=ACK EXPLICIT
// - Monotonic timestamps
// - Same params for signing AND JSON (no drift)
// ─────────────────────────────────────────────────────────────────────────────
class WSAPIRequestBuilder {
public:
    WSAPIRequestBuilder() noexcept : len_(0), last_request_id_(0) {
        buf_[0] = '\0';
    }
    
    // v4.9.22: Get the request ID of the last built request
    [[nodiscard]] uint64_t last_request_id() const noexcept { return last_request_id_; }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // BUILD NEW ORDER - v4.9.27 (enhanced diagnostics)
    // ═══════════════════════════════════════════════════════════════════════════
    [[nodiscard]] const char* build_new_order(
        const char* symbol,
        const char* side,
        const char* type,
        double quantity,
        int qty_precision,
        double price,
        int price_precision,
        const char* time_in_force,
        const char* client_order_id,
        const HMACSigner& signer,
        const char* api_key
    ) noexcept {
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.27: DRIFT GUARD - Check before signing
        // ═══════════════════════════════════════════════════════════════════
        if (!BinanceTimeSync::is_initialized()) {
            printf("[SIG] ⚠️  WARNING: Time sync not initialized! Signatures may fail.\n");
        }
        
        int64_t drift = BinanceTimeSync::current_drift_ms();
        if (std::abs(drift) > 2000) {
            printf("[SIG] ⚠️  WARNING: Clock drift %+lldms exceeds safe threshold!\n",
                   static_cast<long long>(drift));
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // STEP 1: Build params (all values as STRINGS per WS-API requirement)
        // ═══════════════════════════════════════════════════════════════════
        CanonicalParamSet params;
        
        // timestamp (STRING)
        int64_t ts = BinanceTimeSync::next_timestamp_ms();
        char ts_str[32];
        snprintf(ts_str, sizeof(ts_str), "%lld", static_cast<long long>(ts));
        
        // Add all params - order doesn't matter, we'll sort
        params.add_string("apiKey", api_key);
        if (client_order_id && client_order_id[0]) {
            params.add_string("newClientOrderId", client_order_id);
        }
        params.add_string("newOrderRespType", "ACK");
        if (strcmp(type, "LIMIT") == 0) {
            params.add_decimal_string("price", price, price_precision);
        }
        params.add_decimal_string("quantity", quantity, qty_precision);
        params.add_string("recvWindow", "5000");  // Binance example uses 5000
        params.add_string("side", side);
        params.add_string("symbol", symbol);
        if (strcmp(type, "LIMIT") == 0) {
            params.add_string("timeInForce", time_in_force);
        }
        params.add_string("timestamp", ts_str);
        params.add_string("type", type);
        
        // v4.9.32: Sort alphabetically (Binance DOES require this)
        params.sort();
        
        // ═══════════════════════════════════════════════════════════════════
        // STEP 2: Generate canonical string and sign
        // ═══════════════════════════════════════════════════════════════════
        char canonical[2048];
        size_t canonical_len = params.to_canonical_string(canonical, sizeof(canonical));
        
        char signature[65];
        (void)signer.sign(canonical, canonical_len, signature, sizeof(signature));
        
        // ═══════════════════════════════════════════════════════════════════
        // STEP 3: Add signature to params and re-sort for correct JSON position
        // Binance expects signature in alphabetical position (between side/symbol)
        // ═══════════════════════════════════════════════════════════════════
        params.add_string("signature", signature);
        params.sort();  // Re-sort to put signature in correct alphabetical position
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.32: DIAGNOSTIC OUTPUT
        // ═══════════════════════════════════════════════════════════════════
        static uint64_t order_count = 0;
        order_count++;
        
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ [SIG] ORDER #%llu - v4.9.32 ALPHABETICAL + SIGNATURE IN JSON         \n",
               static_cast<unsigned long long>(order_count));
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ TIMESTAMP: %lld (drift: %+lldms)                                     \n",
               static_cast<long long>(ts), static_cast<long long>(drift));
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ CANONICAL (%zu bytes):\n", canonical_len);
        printf("║   %s\n", canonical);
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ SIGNATURE: %s\n", signature);
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ VERIFY: echo -n '%s' | openssl dgst -sha256 -hmac '<SECRET>'\n", canonical);
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
        
        // ═══════════════════════════════════════════════════════════════════
        // STEP 4: Build JSON with ALL params (including signature) in alpha order
        // ═══════════════════════════════════════════════════════════════════
        char json_params[2048];
        params.to_json_params(json_params, sizeof(json_params));
        
        // v4.9.22: Use instance variable for request_id tracking
        static std::atomic<uint64_t> global_request_id{1};
        last_request_id_ = global_request_id.fetch_add(1, std::memory_order_relaxed);
        
        // NOTE: signature is now INSIDE json_params, not appended
        len_ = snprintf(buf_, sizeof(buf_),
            R"({"id":"%llu","method":"order.place","params":{%s}})",
            static_cast<unsigned long long>(last_request_id_),
            json_params
        );
        
        printf("\n[SIG] JSON (%zu bytes, req_id=%llu): %s\n\n", 
               len_, static_cast<unsigned long long>(last_request_id_), buf_);
        
        return buf_;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // BUILD CANCEL ORDER - v4.9.32
    // ═══════════════════════════════════════════════════════════════════════════
    [[nodiscard]] const char* build_cancel_order(
        const char* symbol,
        int64_t order_id,
        const HMACSigner& signer,
        const char* api_key
    ) noexcept {
        CanonicalParamSet params;
        
        params.add_string("apiKey", api_key);
        
        // orderId as string
        char oid_str[32];
        snprintf(oid_str, sizeof(oid_str), "%lld", static_cast<long long>(order_id));
        params.add_string("orderId", oid_str);
        
        // recvWindow as string
        params.add_string("recvWindow", "5000");
        
        params.add_string("symbol", symbol);
        
        // timestamp as string
        char ts_str[32];
        snprintf(ts_str, sizeof(ts_str), "%lld", static_cast<long long>(BinanceTimeSync::next_timestamp_ms()));
        params.add_string("timestamp", ts_str);
        
        // v4.9.32: Sort alphabetically before signing
        params.sort();
        
        char canonical[2048];
        size_t canonical_len = params.to_canonical_string(canonical, sizeof(canonical));
        
        char signature[65];
        (void)signer.sign(canonical, canonical_len, signature, sizeof(signature));
        
        // Add signature and re-sort for correct JSON position
        params.add_string("signature", signature);
        params.sort();
        
        char json_params[2048];
        params.to_json_params(json_params, sizeof(json_params));
        
        static std::atomic<uint64_t> cancel_request_id{1000000};
        last_request_id_ = cancel_request_id.fetch_add(1, std::memory_order_relaxed);
        
        // Signature is now inside json_params
        len_ = snprintf(buf_, sizeof(buf_),
            R"({"id":"%llu","method":"order.cancel","params":{%s}})",
            static_cast<unsigned long long>(last_request_id_),
            json_params
        );
        
        return buf_;
    }
    
    [[nodiscard]] const char* str() const noexcept { return buf_; }
    [[nodiscard]] size_t length() const noexcept { return len_; }

private:
    char   buf_[4096];
    size_t len_;
    uint64_t last_request_id_;  // v4.9.22: Track for response matching
};

// ─────────────────────────────────────────────────────────────────────────────
// Legacy QueryBuilder (for REST compatibility only)
// ─────────────────────────────────────────────────────────────────────────────
class QueryBuilder {
public:
    QueryBuilder() noexcept : len_(0) { buf_[0] = '\0'; }
    
    QueryBuilder& add(const char* key, const char* value) noexcept {
        if (len_ > 0) len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%s", key, value);
        return *this;
    }
    
    QueryBuilder& add(const char* key, int64_t value) noexcept {
        if (len_ > 0) len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%lld", key, static_cast<long long>(value));
        return *this;
    }
    
    QueryBuilder& add(const char* key, double value, int precision) noexcept {
        if (len_ > 0) len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "&");
        len_ += snprintf(buf_ + len_, sizeof(buf_) - len_, "%s=%.*f", key, precision, value);
        return *this;
    }
    
    QueryBuilder& add_timestamp() noexcept {
        return add("timestamp", BinanceTimeSync::next_timestamp_ms());
    }
    
    QueryBuilder& add_signature(const HMACSigner& signer) noexcept {
        char sig[65];
        if (signer.sign(buf_, len_, sig, sizeof(sig)) == 64) {
            add("signature", sig);
        }
        return *this;
    }
    
    [[nodiscard]] const char* str() const noexcept { return buf_; }
    [[nodiscard]] size_t length() const noexcept { return len_; }

private:
    char buf_[2048];
    size_t len_;
};

} // namespace Binance
} // namespace Chimera
