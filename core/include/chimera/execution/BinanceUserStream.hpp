#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>

namespace chimera {

struct ExecutionUpdate {
    std::string client_id;
    std::string symbol;
    std::string status;
    double filled_qty = 0.0;
    double fill_price = 0.0;
    bool is_buy = true;
};

struct AccountUpdate {
    std::string asset;
    double free = 0.0;
    double locked = 0.0;
};

class BinanceUserStream {
public:
    BinanceUserStream(
        const std::string& api_key,
        const std::string& api_secret
    );

    ~BinanceUserStream();

    void start();
    void stop();

    std::function<void(const ExecutionUpdate&)> on_execution;
    std::function<void(const AccountUpdate&)> on_account;

private:
    void worker();
    std::string createListenKey();
    void keepAlive();

private:
    std::string api_key;
    std::string api_secret;

    std::string listen_key;

    std::thread ws_thread;
    std::thread keepalive_thread;

    std::atomic<bool> running{false};
    std::mutex lk_mutex;
};

}
