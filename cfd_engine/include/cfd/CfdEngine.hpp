#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <string>

namespace cfd {

enum class EngineState {
    INIT = 0,
    RUNNING,
    STOPPING,
    DEAD
};

enum class KillReason {
    NONE = 0,
    SOFT_SIGINT,
    SOFT_SIGTERM,
    HARD_TIMEOUT,
    RISK_LIMIT
};

class CfdEngine {
public:
    using PnlCallback = std::function<void(const std::string& source, double pnl_nzd)>;

    CfdEngine();
    ~CfdEngine();

    void start();
    void stop(KillReason reason);

    // NEW
    void set_pnl_callback(PnlCallback cb);

    // (called internally when a fill occurs)
    void emit_pnl(const std::string& tag, double delta_nzd);

    bool alive() const;
    EngineState state() const;
    KillReason kill_reason() const;

private:
    void run();

    std::atomic<bool> running{false};
    std::atomic<bool> is_alive{false};
    std::atomic<EngineState> state_{EngineState::INIT};
    std::atomic<KillReason> kill_reason_{KillReason::NONE};

    PnlCallback pnl_cb_;
    std::thread worker;
};

const char* to_string(EngineState s);
const char* to_string(KillReason r);

} // namespace cfd
