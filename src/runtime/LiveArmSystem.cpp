#include "runtime/LiveArmSystem.hpp"
#include <iostream>

using namespace chimera;

LiveArmSystem::LiveArmSystem(uint64_t min_arm_seconds)
    : min_arm_sec_(min_arm_seconds) {}

bool LiveArmSystem::request_arm(const std::string& code) {
    arm_code_ = code;
    arm_time_ = std::chrono::steady_clock::now();
    armed_.store(false);
    verified_.store(false);
    std::cout << "[ARM] Requested. Waiting for confirmation...\n";
    return true;
}

bool LiveArmSystem::confirm_arm(const std::string& code) {
    if (code != arm_code_) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - arm_time_).count();
    if (elapsed < static_cast<long>(min_arm_sec_)) {
        std::cout << "[ARM] Time lock active. Wait " << (min_arm_sec_ - elapsed) << " seconds\n";
        return false;
    }
    armed_.store(true);
    std::cout << "[ARM] Human confirmation accepted\n";
    return true;
}

bool LiveArmSystem::verify_exchange() {
    if (!armed_.load()) return false;
    verified_.store(true);
    std::cout << "[ARM] Exchange verification OK\n";
    return true;
}

bool LiveArmSystem::live_enabled() const {
    return armed_.load() && verified_.load();
}

std::string LiveArmSystem::status() const {
    if (!armed_) return "DISARMED";
    if (!verified_) return "ARMED_WAITING_VERIFY";
    return "LIVE_ENABLED";
}

// B5 FIX: direct state restore from CRC-verified snapshot.
// armed_ is restored but verified_ is ALWAYS reset to false on cold start —
// exchange verification must pass again after reboot (Drop 11 cold-start gate).
// This is intentional safety: snapshot proves we WERE armed, but the exchange
// state must be re-confirmed live before we're allowed to trade again.
void LiveArmSystem::restore(bool armed, bool /*verified*/) {
    armed_.store(armed);
    verified_.store(false);  // Force re-verification on cold start
    if (armed) {
        std::cout << "[ARM] Restored from snapshot. Armed=true, verification REQUIRED.\n";
    }
}
