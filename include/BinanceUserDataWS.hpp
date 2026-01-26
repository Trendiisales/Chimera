#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

class BinanceUserDataWS {
public:
    using FillHandler = std::function<void(std::string, std::string, double, double)>;

    BinanceUserDataWS(const std::string& listen_key, FillHandler handler);

    void start();
    void stop();

    FillHandler handler_;  // Public for callback access

private:
    void run();

    std::string listen_key_;
    std::thread thread_;
    std::atomic<bool> running_;
};
