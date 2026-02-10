#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <string>

/* ============================================================
   TIME
   ============================================================ */

static uint64_t mono_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

/* ============================================================
   WATCHDOG (FIXED)
   ============================================================ */

static uint64_t wd_last = 0;

static void watchdog_tick() {
    uint64_t now = mono_ms();
    if (wd_last == 0) {
        wd_last = now;
        return;
    }

    uint64_t elapsed = now - wd_last;

    if (elapsed > 3000 && elapsed < 60000) {
        std::fprintf(stderr,
            "[WATCHDOG] Stall warning: %llu ms\n",
            (unsigned long long)elapsed);
    }

    wd_last = now;
}

/* ============================================================
   LATENCY WINDOW
   ============================================================ */

class LatencyWindow {
public:
    explicit LatencyWindow(size_t cap = 512)
        : cap_(cap), ewma_(0.0), has_ewma_(false) {}

    void push(double ms) {
        if (ms <= 0.0 || ms > 1000.0)
            return;

        if (!has_ewma_) {
            ewma_ = ms;
            has_ewma_ = true;
        } else {
            ewma_ = ewma_ * 0.90 + ms * 0.10;
        }

        buf_.push_back(ms);
        if (buf_.size() > cap_)
            buf_.erase(buf_.begin());
    }

    size_t size() const { return buf_.size(); }

    double last() const {
        return buf_.empty() ? -1.0 : buf_.back();
    }

    double ewma() const { return has_ewma_ ? ewma_ : -1.0; }

    double pct(double p) const {
        if (buf_.size() < 20)
            return -1.0;

        std::vector<double> v = buf_;
        std::sort(v.begin(), v.end());
        size_t idx = size_t(p * (v.size() - 1));
        return v[idx];
    }

private:
    size_t cap_;
    std::vector<double> buf_;
    double ewma_;
    bool has_ewma_;
};

/* ============================================================
   LATENCY STATE
   ============================================================ */

enum class LatState {
    WARMUP,
    FAST,
    NORMAL,
    DEGRADED,
    HALT
};

static const char* ls(LatState s) {
    switch (s) {
        case LatState::WARMUP: return "WARMUP";
        case LatState::FAST: return "FAST";
        case LatState::NORMAL: return "NORMAL";
        case LatState::DEGRADED: return "DEGRADED";
        case LatState::HALT: return "HALT";
    }
    return "?";
}

/* ============================================================
   GOVERNOR
   ============================================================ */

class LatencyGovernor {
public:
    LatencyGovernor()
        : start_ms_(mono_ms()), state_(LatState::WARMUP) {}

    void on_rtt(double ms) {
        watchdog_tick();
        window_.push(ms);
        update();
    }

    bool allow_exec(const std::string& sym) {
        watchdog_tick();

        if (state_ == LatState::HALT)
            return false;

        if (sym == "XAUUSD")
            return state_ == LatState::FAST;

        if (sym == "XAGUSD")
            return state_ == LatState::FAST || state_ == LatState::NORMAL;

        return true;
    }

    void dump() const {
        double cur = window_.last();
        double p95 = window_.pct(0.95);
        double p99 = window_.pct(0.99);

        std::printf(
            "[LATENCY] state=%s cur=%.2f p95=%.2f p99=%.2f ewma=%.2f samples=%zu\n",
            ls(state_),
            cur,
            p95,
            p99,
            window_.ewma(),
            window_.size()
        );
    }

private:
    void update() {
        uint64_t now = mono_ms();

        if (window_.size() < 20 && now - start_ms_ < 5000) {
            state_ = LatState::WARMUP;
            return;
        }

        double cur = window_.last();
        double p95 = window_.pct(0.95);
        double p99 = window_.pct(0.99);

        if (p99 > 25.0 || cur > 25.0) {
            state_ = LatState::HALT;
        }
        else if (p95 > 12.0) {
            state_ = LatState::DEGRADED;
        }
        else if (p95 <= 6.0 && window_.ewma() <= 6.0) {
            state_ = LatState::FAST;
        }
        else {
            state_ = LatState::NORMAL;
        }
    }

    uint64_t start_ms_;
    LatencyWindow window_;
    LatState state_;
};

/* ============================================================
   GLOBAL INSTANCE
   ============================================================ */

static LatencyGovernor g_latency;

/* ============================================================
   PUBLIC API (namespace for compatibility)
   ============================================================ */

namespace LatencyRouter {

void recordRtt(double rtt_ms) {
    g_latency.on_rtt(rtt_ms);
}

bool allowEntry(const std::string& symbol) {
    return g_latency.allow_exec(symbol);
}

void dumpStatus() {
    g_latency.dump();
}

} // namespace LatencyRouter
