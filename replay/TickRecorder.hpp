#pragma once
#include <fstream>
#include <string>
#include "../tier3/TickData.hpp"

class TickRecorder {
public:
    TickRecorder(const std::string& path)
        : file_(path, std::ios::binary | std::ios::out) {}

    inline void record(const std::string& sym, const tier3::TickData& t) {
        uint32_t len = sym.size();
        file_.write((char*)&len, sizeof(len));
        file_.write(sym.data(), len);
        file_.write((char*)&t, sizeof(t));
    }

private:
    std::ofstream file_;
};
