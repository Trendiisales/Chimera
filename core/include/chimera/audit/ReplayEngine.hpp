#pragma once

#include <string>
#include <fstream>
#include <vector>

#include "chimera/audit/BinaryEventLog.hpp"

namespace chimera {

class ReplayEngine {
public:
    explicit ReplayEngine(
        const std::string& path
    );

    ~ReplayEngine();

    bool next(
        EventHeader& hdr,
        std::vector<uint8_t>& payload
    );

    void reset();

private:
    std::ifstream in;
    std::string path;
};

}
