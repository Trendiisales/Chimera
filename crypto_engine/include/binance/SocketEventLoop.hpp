#pragma once
#include <functional>
#include <atomic>

namespace binance {

/*
 Platform-neutral socket event loop skeleton.
 This is the ONLY place epoll/kqueue will ever live.
*/
class SocketEventLoop {
public:
    using ReadCallback = std::function<void()>;

    SocketEventLoop();
    ~SocketEventLoop();

    void set_read_callback(const ReadCallback& cb);
    void start();
    void stop();

private:
    ReadCallback on_read;
    std::atomic<bool> running{false};

#if defined(__APPLE__)
    int kq{-1};
#elif defined(__linux__)
    int ep{-1};
#endif
};

}
