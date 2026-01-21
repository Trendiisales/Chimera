#pragma once

#include <string>
#include <cstdint>

namespace chimera {

class LogRotator {
public:
    LogRotator(
        const std::string& base,
        uint64_t max_bytes
    );

    std::string current();

    void rotateIfNeeded();

private:
    std::string base_path;
    uint64_t max_size;
    int index = 0;
};

}
