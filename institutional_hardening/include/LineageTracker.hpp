#pragma once
#include <string>

namespace chimera {
namespace hardening {

struct ResearchLineage {
    std::string engine_version;
    std::string strategy_hash;
    std::string regime_model_version;
    std::string build_id;
    int64_t timestamp;
};

class LineageTracker {
public:
    static void setVersion(const std::string& engine, const std::string& version);
    static ResearchLineage capture(const std::string& engine);
};

}} // namespace chimera::hardening
