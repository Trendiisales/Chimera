#pragma once
#include <string>

namespace chimera {
namespace hardening {

// Load secrets from environment variables (NOT from keys.json)
class Secrets {
public:
    static std::string get(const std::string& key);
    static std::string getRequired(const std::string& key);
};

}} // namespace chimera::hardening
