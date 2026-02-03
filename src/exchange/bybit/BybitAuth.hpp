#pragma once
#include <string>
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

namespace chimera {

// Bybit V5 uses two credentials: API Key and Secret Key.
// Signing: HMAC-SHA256(secret, apiKey + recvWindow + timestamp + payload) → hex.
// payload = query string (no leading ?) for GET, raw JSON body for POST.
// recvWindow in milliseconds — default 5000.
class BybitAuth {
public:
    BybitAuth(const std::string& key, const std::string& secret)
        : api_key_(key), api_secret_(secret) {}

    // Epoch milliseconds as string — Bybit timestamp format
    static std::string now_ms() {
        using namespace std::chrono;
        return std::to_string(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
        );
    }

    // THREAD-SAFE: stack-local digest buffer. Matches BinanceAuth pattern.
    // payload: query string for GET, body for POST.
    std::string sign(const std::string& timestamp,
                     const std::string& payload,
                     const std::string& recv_window = "5000") const {
        std::string pre_sign = api_key_ + recv_window + timestamp + payload;

        unsigned char digest[32];   // stack-local
        unsigned int  digest_len = 0;

        HMAC(EVP_sha256(),
             api_secret_.c_str(),  static_cast<int>(api_secret_.size()),
             reinterpret_cast<const unsigned char*>(pre_sign.c_str()),
             static_cast<int>(pre_sign.size()),
             digest, &digest_len);

        std::ostringstream out;
        for (unsigned int i = 0; i < digest_len; ++i)
            out << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(digest[i]);
        return out.str();
    }

    const std::string& api_key() const { return api_key_; }

private:
    std::string api_key_;
    std::string api_secret_;
};

} // namespace chimera
