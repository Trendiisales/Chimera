#pragma once
#include <string>

namespace Chimera {

class BinaryReplay {
public:
    explicit BinaryReplay(const std::string& path);
    void run();
private:
    int fd_;
    size_t size_;
    const uint8_t* base_;
};

}
