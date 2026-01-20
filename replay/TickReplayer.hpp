#pragma once
#include <fstream>
#include <functional>
#include <thread>
#include <chrono>
#include "../tier3/TickData.hpp"

class TickReplayer {
public:
    using TickCB = std::function<void(const std::string&, const tier3::TickData&)>;

    TickReplayer(const std::string& path, TickCB cb)
        : file_(path, std::ios::binary | std::ios::in), cb_(cb) {}

    void run(double speed = 1.0) {
        std::thread([this, speed]() {
            while (file_) {
                uint32_t len;
                if (!file_.read((char*)&len, sizeof(len))) break;
                std::string sym(len, 0);
                file_.read(&sym[0], len);

                tier3::TickData t;
                file_.read((char*)&t, sizeof(t));

                cb_(sym, t);
                std::this_thread::sleep_for(
                    std::chrono::microseconds((int)(1000 / speed)));
            }
        }).detach();
    }

private:
    std::ifstream file_;
    TickCB cb_;
};
