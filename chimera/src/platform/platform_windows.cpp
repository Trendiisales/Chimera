#ifdef _WIN32

#include "../../include/platform/platform.hpp"
#include <chrono>
#include <thread>

namespace plat {

bool init() {
    WSADATA w;
    return WSAStartup(MAKEWORD(2,2), &w) == 0;
}

void cleanup() {
    WSACleanup();
}

int last_error() {
    return WSAGetLastError();
}

bool would_block(int e) {
    return e == WSAEWOULDBLOCK;
}

socket_t create_tcp() {
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

bool set_nonblocking(socket_t s, bool enable) {
    u_long mode = enable ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

bool set_nodelay(socket_t s) {
    int flag = 1;
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
        (const char*)&flag, sizeof(flag)) == 0;
}

bool set_reuseaddr(socket_t s) {
    int flag = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&flag, sizeof(flag)) == 0;
}

int socket_send(socket_t s, const void* buf, int len) {
    return send(s, (const char*)buf, len, 0);
}

int socket_recv(socket_t s, void* buf, int len) {
    return recv(s, (char*)buf, len, 0);
}

bool socket_shutdown(socket_t s) {
    return shutdown(s, SD_BOTH) == 0;
}

void socket_close(socket_t s) {
    closesocket(s);
}

void sleep_us(uint64_t usec) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

uint64_t monotonic_time_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

}

#endif

