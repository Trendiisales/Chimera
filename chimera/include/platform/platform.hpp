#pragma once
#include <cstdint>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_FD = -1;
#endif

namespace plat {
bool init();
void cleanup();
int last_error();
bool would_block(int err);
socket_t create_tcp();
bool set_nonblocking(socket_t s, bool enable);
bool set_nodelay(socket_t s);
bool set_reuseaddr(socket_t s);
int socket_send(socket_t s, const void* buf, int len);
int socket_recv(socket_t s, void* buf, int len);
bool socket_shutdown(socket_t s);
void socket_close(socket_t s);
void sleep_us(uint64_t usec);
uint64_t monotonic_time_us();
}
