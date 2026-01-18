#pragma once
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char* CHIMERA_SOCK = "/tmp/chimera.sock";

inline int ipc_connect() {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CHIMERA_SOCK);

    connect(fd, (sockaddr*)&addr, sizeof(addr));
    return fd;
}

inline void ipc_send(int fd, const std::string& msg) {
    if (fd >= 0) send(fd, msg.c_str(), msg.size(), 0);
}
