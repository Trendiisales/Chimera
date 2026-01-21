#include "chimera/audit/ReplayEngine.hpp"

namespace chimera {

ReplayEngine::ReplayEngine(
    const std::string& p
) : path(p) {
    in.open(
        path,
        std::ios::binary |
        std::ios::in
    );
}

ReplayEngine::~ReplayEngine() {
    if (in.is_open()) {
        in.close();
    }
}

bool ReplayEngine::next(
    EventHeader& hdr,
    std::vector<uint8_t>& payload
) {
    if (!in.is_open()) return false;

    // Read header
    if (!in.read(
        reinterpret_cast<char*>(&hdr),
        sizeof(hdr)
    )) {
        return false;
    }

    // Read payload
    payload.resize(hdr.size);

    if (hdr.size > 0) {
        if (!in.read(
            reinterpret_cast<char*>(payload.data()),
            hdr.size
        )) {
            return false;
        }
    }

    return true;
}

void ReplayEngine::reset() {
    if (in.is_open()) {
        in.close();
    }
    
    in.open(
        path,
        std::ios::binary |
        std::ios::in
    );
}

}
