#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>

#include "../tier3/TickData.hpp"

class BinanceWSClient {
public:
    using TickCB = std::function<void(const std::string& stream,
                                      const tier3::TickData&)>;

    BinanceWSClient(const std::string& host,
                    const std::string& port,
                    const std::string& stream);

    void setCallback(TickCB cb);
    void start();
    void stop();

private:
    void run();

    std::string host_;
    std::string port_;
    std::string stream_;

    TickCB cb_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
