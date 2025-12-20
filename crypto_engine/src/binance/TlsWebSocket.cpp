#include "binance/TlsWebSocket.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <random>
#include <chrono>

namespace binance {

static std::string fixed_key() {
    return "dGhlIHNhbXBsZSBub25jZQ==";
}

static uint32_t rand_mask() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return rng();
}

TlsWebSocket::TlsWebSocket(const std::string& h, int p, const std::string& pa)
    : host(h), port(p), path(pa) {}

TlsWebSocket::~TlsWebSocket() {
    stop();
}

bool TlsWebSocket::tcp_connect() {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        return false;

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
        return false;

    if (::connect(sock, res->ai_addr, res->ai_addrlen) != 0)
        return false;

    freeaddrinfo(res);
    return true;
}

bool TlsWebSocket::tls_handshake() {
    SSL_library_init();
    SSL_load_error_strings();

    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx)
        return false;

    ssl = SSL_new(static_cast<SSL_CTX*>(ssl_ctx));
    SSL_set_fd(static_cast<SSL*>(ssl), sock);

    return SSL_connect(static_cast<SSL*>(ssl)) == 1;
}

bool TlsWebSocket::ws_handshake() {
    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + fixed_key() + "\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    SSL_write(static_cast<SSL*>(ssl), req.data(), req.size());

    char buf[1024];
    int n = SSL_read(static_cast<SSL*>(ssl), buf, sizeof(buf));
    if (n <= 0)
        return false;

    return std::string(buf, n).find("101") != std::string::npos;
}

bool TlsWebSocket::connect() {
    return tcp_connect() && tls_handshake() && ws_handshake();
}

void TlsWebSocket::set_on_message(OnMessage cb) {
    on_message = cb;
}

void TlsWebSocket::send_frame(uint8_t opcode, const std::string& payload) {
    uint8_t hdr[14];
    size_t hlen = 0;

    hdr[hlen++] = 0x80 | (opcode & 0x0F);

    size_t len = payload.size();
    if (len <= 125) {
        hdr[hlen++] = 0x80 | static_cast<uint8_t>(len);
    } else if (len <= 0xFFFF) {
        hdr[hlen++] = 0x80 | 126;
        hdr[hlen++] = (len >> 8) & 0xFF;
        hdr[hlen++] = len & 0xFF;
    } else {
        hdr[hlen++] = 0x80 | 127;
        for (int i = 7; i >= 0; --i)
            hdr[hlen++] = (len >> (i * 8)) & 0xFF;
    }

    uint32_t mask = rand_mask();
    std::memcpy(hdr + hlen, &mask, 4);
    hlen += 4;

    SSL_write(static_cast<SSL*>(ssl), hdr, hlen);

    std::string masked = payload;
    uint8_t* m = reinterpret_cast<uint8_t*>(&mask);
    for (size_t i = 0; i < masked.size(); ++i)
        masked[i] ^= m[i & 3];

    if (!masked.empty())
        SSL_write(static_cast<SSL*>(ssl), masked.data(), masked.size());
}

void TlsWebSocket::send_text(const std::string& msg) {
    send_frame(0x1, msg);
}

void TlsWebSocket::send_ping() {
    send_frame(0x9, "");
}

void TlsWebSocket::run() {
    running = true;
    auto last_ping = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_ping > std::chrono::seconds(30)) {
            send_ping();
            last_ping = now;
        }

        uint8_t hdr[2];
        if (SSL_read(static_cast<SSL*>(ssl), hdr, 2) != 2)
            break;

        size_t len = hdr[1] & 0x7F;
        if (len == 126) {
            uint8_t ext[2];
            SSL_read(static_cast<SSL*>(ssl), ext, 2);
            len = (ext[0] << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            SSL_read(static_cast<SSL*>(ssl), ext, 8);
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | ext[i];
        }

        std::string payload(len, '\0');
        if (len)
            SSL_read(static_cast<SSL*>(ssl), payload.data(), len);

        if ((hdr[0] & 0x0F) == 0x1 && on_message)
            on_message(payload);
    }
}

void TlsWebSocket::stop() {
    running = false;

    if (ssl) {
        SSL_shutdown(static_cast<SSL*>(ssl));
        SSL_free(static_cast<SSL*>(ssl));
        ssl = nullptr;
    }
    if (ssl_ctx) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx));
        ssl_ctx = nullptr;
    }
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
}

}
