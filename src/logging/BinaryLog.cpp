#include "logging/BinaryLog.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

using namespace Chimera;

BinaryLogWriter::BinaryLogWriter(const std::string& path)
    : fd_(-1), base_(nullptr), capacity_(1ULL << 30), seq_(0), offset_(0) {
    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    ::ftruncate(fd_, capacity_);
    base_ = static_cast<uint8_t*>(::mmap(nullptr, capacity_, PROT_WRITE | PROT_READ, MAP_SHARED, fd_, 0));
}

BinaryLogWriter::~BinaryLogWriter() {
    if (base_) ::munmap(base_, capacity_);
    if (fd_ >= 0) ::close(fd_);
}

bool BinaryLogWriter::write(const void* data, uint32_t size, LogRecordType type, uint16_t venue) {
    BinaryLogHeader h;
    h.seq = seq_.fetch_add(1, std::memory_order_relaxed);
    h.ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    h.type = static_cast<uint16_t>(type);
    h.venue = venue;
    h.size = size;

    size_t total = sizeof(h) + size;
    if (offset_ + total >= capacity_) return false;

    std::memcpy(base_ + offset_, &h, sizeof(h));
    std::memcpy(base_ + offset_ + sizeof(h), data, size);
    offset_ += total;
    return true;
}

uint64_t BinaryLogWriter::sequence() const {
    return seq_.load(std::memory_order_relaxed);
}
