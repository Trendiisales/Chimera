#pragma once
#include <atomic>
#include <string>
#include <chrono>

namespace chimera {

class LiveArmSystem {
public:
    explicit LiveArmSystem(uint64_t min_arm_seconds);

    bool request_arm(const std::string& code);
    bool confirm_arm(const std::string& code);
    bool verify_exchange();
    bool live_enabled() const;
    std::string status() const;

    // B5 FIX: snapshot restore path.
    // Does NOT go through time-lock or confirmation — restores exact prior state.
    // Safe: snapshot is CRC-verified before restore is ever called.
    // After restore, system is in whatever state it was when snapshot was taken.
    // Live trading still requires exchange verification on cold start (Drop 11 gate).
    void restore(bool armed, bool verified);

private:
    uint64_t min_arm_sec_;
    std::atomic<bool> armed_{false};
    std::atomic<bool> verified_{false};
    std::string arm_code_;
    std::chrono::steady_clock::time_point arm_time_;
};

}
