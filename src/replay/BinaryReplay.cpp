#include "replay/BinaryReplay.hpp"
#include "logging/BinaryLog.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

using namespace Chimera;

BinaryReplay::BinaryReplay(const std::string& path)
    : fd_(-1), size_(0), base_(nullptr) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    size_ = ::lseek(fd_, 0, SEEK_END);
    base_ = static_cast<uint8_t*>(::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
}

void BinaryReplay::run() {
    size_t off = 0;
    while (off + sizeof(BinaryLogHeader) < size_) {
        auto* h = reinterpret_cast<const BinaryLogHeader*>(base_ + off);
        off += sizeof(BinaryLogHeader) + h->size;
        std::cout << "REPLAY seq=" << h->seq << " type=" << h->type << " venue=" << h->venue << "\n";
    }
}
