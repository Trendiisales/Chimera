#pragma once
#include <string>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

namespace chimera {

class BinanceAuth {
public:
    BinanceAuth(const std::string& key, const std::string& secret)
        : api_key_(key), api_secret_(secret) {}

    // THREAD-SAFE: digest is now a stack-local buffer.
    // The original used a static buffer — two threads signing concurrently
    // would overwrite each other's HMAC output before hex conversion.
    std::string sign(const std::string& payload) const {
        unsigned char digest[32];   // stack-local, one per call frame
        unsigned int  digest_len = 0;

        HMAC(EVP_sha256(),
             api_secret_.c_str(),  static_cast<int>(api_secret_.size()),
             reinterpret_cast<const unsigned char*>(payload.c_str()),
             static_cast<int>(payload.size()),
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
