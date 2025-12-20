#include "binance/BinaryLogWriter.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <chrono>
#include <stdexcept>

namespace binance {

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

BinaryLogWriter::BinaryLogWriter(
    const std::string& path,
    const std::string& symbol,
    size_t map_size_bytes)
    : map_size(map_size_bytes) {

    fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        throw std::runtime_error("open failed");

    if (ftruncate(fd, map_size) != 0)
        throw std::runtime_error("ftruncate failed");

    map = static_cast<uint8_t*>(
        mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (map == MAP_FAILED)
        throw std::runtime_error("mmap failed");

    BinaryLogHeader hdr{};
    hdr.magic = 0x424C4F47; // BLOG
    hdr.version = 1;
    hdr.header_size = sizeof(BinaryLogHeader);
    hdr.start_ns = now_ns();
    hdr.symbol_len = static_cast<uint32_t>(symbol.size());

    write_bytes(&hdr, sizeof(hdr));
    write_bytes(symbol.data(), symbol.size());
}

BinaryLogWriter::~BinaryLogWriter() {
    if (map && map != MAP_FAILED) {
        msync(map, write_off, MS_SYNC);
        munmap(map, map_size);
    }
    if (fd >= 0)
        close(fd);
}

void BinaryLogWriter::ensure_space(size_t need) {
    if (write_off + need > map_size)
        throw std::runtime_error("binary log overflow");
}

void BinaryLogWriter::write_bytes(const void* src, size_t bytes) {
    ensure_space(bytes);
    std::memcpy(map + write_off, src, bytes);
    write_off += bytes;
}

void BinaryLogWriter::write_snapshot(
    const void* data, size_t bytes, uint64_t ts_ns) {

    RecordHeader rh{};
    rh.type = static_cast<uint8_t>(RecordType::SNAPSHOT);
    rh.size = static_cast<uint16_t>(bytes);
    rh.ts_ns = ts_ns;

    write_bytes(&rh, sizeof(rh));
    write_bytes(data, bytes);
}

void BinaryLogWriter::write_depth_delta(
    uint64_t U, uint64_t u,
    const void* payload, size_t bytes,
    uint64_t ts_ns) {

    RecordHeader rh{};
    rh.type = static_cast<uint8_t>(RecordType::DEPTH_DELTA);
    rh.size = static_cast<uint16_t>(sizeof(DepthDeltaRecord) + bytes);
    rh.ts_ns = ts_ns;

    DepthDeltaRecord dr{};
    dr.U = U;
    dr.u = u;
    dr.bids_count = 0;
    dr.asks_count = 0;

    write_bytes(&rh, sizeof(rh));
    write_bytes(&dr, sizeof(dr));
    write_bytes(payload, bytes);
}

void BinaryLogWriter::flush() {
    msync(map, write_off, MS_SYNC);
}

} // namespace binance
