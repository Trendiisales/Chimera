#pragma once
#include <mutex>
#include <unordered_map>
#include <string>
#include <atomic>

namespace chimera {
namespace hardening {

// Single authoritative gate for ALL trade decisions
// Kill switches, Alpha Governor, Risk Pools must write through this
class TradeAuthority {
public:
    static TradeAuthority& instance();

    // Check if trading is allowed
    bool allow(const std::string& engine, const std::string& symbol);

    // Engine-level control
    void disableEngine(const std::string& engine, const std::string& reason);
    void enableEngine(const std::string& engine);
    bool isEngineEnabled(const std::string& engine) const;

    // Global control
    void killAll(const std::string& reason);
    void reviveAll();
    bool isKilled() const;

    // Audit trail
    std::string getDisableReason(const std::string& engine) const;

private:
    TradeAuthority() = default;
    mutable std::mutex mtx_;
    std::atomic<bool> global_kill_{false};
    std::unordered_map<std::string, bool> engine_enabled_;
    std::unordered_map<std::string, std::string> disable_reasons_;
};

}} // namespace chimera::hardening
