#pragma once
#include <string>

namespace chimera {

std::string json_kv(const std::string& k,
                    const std::string& v,
                    bool last = false);

std::string json_kv(const std::string& k,
                    double v,
                    bool last = false);

std::string json_kv(const std::string& k,
                    uint64_t v,
                    bool last = false);

}
