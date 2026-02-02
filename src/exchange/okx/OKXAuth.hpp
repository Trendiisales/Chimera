#pragma once
#include <string>
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace chimera {

// OKX requires three credentials: API Key, Secret Key, Passphrase.
// Signing: HMAC-SHA256(secret, timestamp + method + path + body) → base64.
// Path includes query string for GET requests.
// Body is the raw JSON string for POST requests, empty for GET.
class OKXAuth {
public:
    OKXAuth(const std::string& key, const std::string& secret, const std::string& passphrase)
        : api_key_(key), api_secret_(secret), passphrase_(passphrase) {}

    // Epoch seconds as string — OKX timestamp format
    static std::string now_sec() {
        using namespace std::chrono;
        return std::to_string(
            duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
        );
    }

    // THREAD-SAFE: all buffers are stack-local. No static state.
    // Returns base64-encoded HMAC-SHA256 signature.
    std::string sign(const std::string& timestamp,
                     const std::string& method,
                     const std::string& path,
                     const std::string& body = "") const {
        std::string pre_sign = timestamp + method + path + body;

        unsigned char digest[32];   // stack-local
        unsigned int  digest_len = 0;

        HMAC(EVP_sha256(),
             api_secret_.c_str(),  static_cast<int>(api_secret_.size()),
             reinterpret_cast<const unsigned char*>(pre_sign.c_str()),
             static_cast<int>(pre_sign.size()),
             digest, &digest_len);

        // Base64 encode — stack-local BIO chain
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new(BIO_s_mem());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // single-line output
        BIO_push(b64, mem);
        BIO_write(b64, digest, static_cast<int>(digest_len));
        BIO_flush(b64);

        BUF_MEM* buf;
        BIO_get_mem_ptr(mem, &buf);
        std::string result(buf->data, buf->length);

        BIO_free_all(b64);  // frees both b64 and mem
        return result;
    }

    const std::string& api_key() const    { return api_key_; }
    const std::string& passphrase() const { return passphrase_; }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string passphrase_;
};

} // namespace chimera
