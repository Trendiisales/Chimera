#include <fstream>
#include <iostream>
#include "forensics/EventTypes.hpp"
#include "forensics/CRC32.hpp"

using namespace chimera;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: replay_validator <log.bin>\n";
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open log\n";
        return 1;
    }

    int event_count = 0;
    while (true) {
        EventHeader hdr;
        if (!file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)))
            break;

        std::string payload;
        payload.resize(hdr.size);
        file.read(payload.data(), hdr.size);

        uint32_t crc = CRC32::compute(payload.data(), hdr.size);
        if (crc != hdr.crc) {
            std::cerr << "[CRC ERROR] causal=" << hdr.causal_id << "\n";
            return 2;
        }

        std::cout << "[OK] ts=" << hdr.ts_ns
                  << " causal=" << hdr.causal_id
                  << " type=" << static_cast<int>(hdr.type)
                  << " size=" << hdr.size << "\n";
        ++event_count;
    }

    std::cout << "[REPLAY] " << event_count << " events validated OK\n";
    return 0;
}
