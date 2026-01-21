#include "LineageTracker.hpp"
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace chimera {
namespace hardening {

static std::mutex lineage_mutex;
static std::unordered_map<std::string, std::string> engine_versions;

void LineageTracker::setVersion(const std::string& engine, const std::string& version) {
    std::lock_guard<std::mutex> lock(lineage_mutex);
    engine_versions[engine] = version;
}

ResearchLineage LineageTracker::capture(const std::string& engine) {
    std::lock_guard<std::mutex> lock(lineage_mutex);
    
    ResearchLineage l{};
    auto it = engine_versions.find(engine);
    l.engine_version = (it != engine_versions.end()) ? it->second : "unknown";
    l.strategy_hash = "sha256_placeholder"; // TODO: compute from strategy code
    l.regime_model_version = "v1.0";
    l.build_id = __DATE__ " " __TIME__;
    l.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    return l;
}

}} // namespace chimera::hardening
