#include "TradeAuthority.hpp"
#include <iostream>

namespace chimera {
namespace hardening {

TradeAuthority& TradeAuthority::instance() {
    static TradeAuthority inst;
    return inst;
}

bool TradeAuthority::allow(const std::string& engine, const std::string&) {
    if (global_kill_.load()) return false;
    
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = engine_enabled_.find(engine);
    if (it == engine_enabled_.end()) return true; // Default enabled
    return it->second;
}

void TradeAuthority::disableEngine(const std::string& engine, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mtx_);
    engine_enabled_[engine] = false;
    disable_reasons_[engine] = reason;
    std::cout << "[TradeAuthority] DISABLED " << engine << ": " << reason << std::endl;
}

void TradeAuthority::enableEngine(const std::string& engine) {
    std::lock_guard<std::mutex> lock(mtx_);
    engine_enabled_[engine] = true;
    disable_reasons_.erase(engine);
    std::cout << "[TradeAuthority] ENABLED " << engine << std::endl;
}

bool TradeAuthority::isEngineEnabled(const std::string& engine) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = engine_enabled_.find(engine);
    return it == engine_enabled_.end() || it->second;
}

void TradeAuthority::killAll(const std::string& reason) {
    global_kill_.store(true);
    std::cout << "[TradeAuthority] GLOBAL KILL: " << reason << std::endl;
}

void TradeAuthority::reviveAll() {
    global_kill_.store(false);
    std::cout << "[TradeAuthority] GLOBAL REVIVE" << std::endl;
}

bool TradeAuthority::isKilled() const {
    return global_kill_.load();
}

std::string TradeAuthority::getDisableReason(const std::string& engine) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = disable_reasons_.find(engine);
    return it != disable_reasons_.end() ? it->second : "";
}

}} // namespace chimera::hardening
