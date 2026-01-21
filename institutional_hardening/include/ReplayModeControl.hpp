#pragma once
#include <atomic>

namespace chimera {
namespace hardening {

enum class ExecutionMode {
    LIVE,
    REPLAY
};

// Global replay mode flag - ALL adaptive systems must check this
class ReplayModeControl {
public:
    static void setMode(ExecutionMode m);
    static ExecutionMode getMode();
    static bool isReplay();

private:
    static std::atomic<ExecutionMode> mode_;
};

}} // namespace chimera::hardening
