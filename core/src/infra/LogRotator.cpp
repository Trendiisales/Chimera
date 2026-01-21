#include "chimera/infra/LogRotator.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace chimera {

LogRotator::LogRotator(
    const std::string& base,
    uint64_t max_bytes
) : base_path(base),
    max_size(max_bytes) {}

std::string LogRotator::current() {
    return base_path + "_" +
           std::to_string(index) +
           ".bin";
}

void LogRotator::rotateIfNeeded() {
    if (!fs::exists(current())) return;

    auto sz = fs::file_size(current());
    if (sz >= max_size) {
        index++;
    }
}

}
