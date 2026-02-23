#include "TelemetryJson.hpp"

namespace chimera {

std::string json_kv(const std::string& k,
                    const std::string& v,
                    bool last) {
    return "\"" + k + "\":\"" + v + "\"" + (last ? "" : ",");
}

std::string json_kv(const std::string& k,
                    double v,
                    bool last) {
    return "\"" + k + "\":" + std::to_string(v) + (last ? "" : ",");
}

std::string json_kv(const std::string& k,
                    uint64_t v,
                    bool last) {
    return "\"" + k + "\":" + std::to_string(v) + (last ? "" : ",");
}

}
