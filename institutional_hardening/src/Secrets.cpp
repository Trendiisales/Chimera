#include "Secrets.hpp"
#include <cstdlib>
#include <stdexcept>

namespace chimera {
namespace hardening {

std::string Secrets::get(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : "";
}

std::string Secrets::getRequired(const std::string& key) {
    std::string val = get(key);
    if (val.empty()) {
        throw std::runtime_error("Required environment variable not set: " + key);
    }
    return val;
}

}} // namespace chimera::hardening
