#pragma once
#include <string>

namespace Chimera {

class BinaryReplayEngine {
public:
    explicit BinaryReplayEngine(const std::string& path);
    void run();

private:
    int fd_;
    size_t size_;
    const unsigned char* base_;
};

}
