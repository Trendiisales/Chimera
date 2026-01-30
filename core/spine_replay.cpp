#include "core/spine_replay.hpp"
#include <fstream>
#include <cstring>

SpineReplay::SpineReplay(const std::string& bin_path)
    : pos(0) {
    load_file(bin_path);
}

bool SpineReplay::load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good())
        return false;

    buffer.assign(std::istreambuf_iterator<char>(f),
                  std::istreambuf_iterator<char>());
    return true;
}

bool SpineReplay::next(SpineEvent& out) {
    if (pos + sizeof(uint64_t) * 2 + sizeof(uint32_t) + sizeof(uint64_t) > buffer.size())
        return false;

    std::memcpy(&out.id, &buffer[pos], sizeof(uint64_t));
    pos += sizeof(uint64_t);

    std::memcpy(&out.ts_ns, &buffer[pos], sizeof(uint64_t));
    pos += sizeof(uint64_t);

    uint32_t t;
    std::memcpy(&t, &buffer[pos], sizeof(uint32_t));
    pos += sizeof(uint32_t);
    out.type = static_cast<EventType>(t);

    uint64_t len;
    std::memcpy(&len, &buffer[pos], sizeof(uint64_t));
    pos += sizeof(uint64_t);

    if (pos + len > buffer.size())
        return false;

    out.payload.assign(&buffer[pos], &buffer[pos + len]);
    pos += len;

    return true;
}
