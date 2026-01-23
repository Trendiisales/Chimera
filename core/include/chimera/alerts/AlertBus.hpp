#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace chimera::alerts {

struct Alert {
    uint64_t ts_ns;
    std::string level; // INFO, WARN, CRITICAL
    std::string source;
    std::string message;
};

class AlertBus {
public:
    static void emit(const Alert& a) {
        std::lock_guard<std::mutex> lk(mtx());
        alerts().push_back(a);
    }

    static std::vector<Alert> snapshot() {
        std::lock_guard<std::mutex> lk(mtx());
        return alerts();
    }

private:
    static std::vector<Alert>& alerts() {
        static std::vector<Alert> a;
        return a;
    }

    static std::mutex& mtx() {
        static std::mutex m;
        return m;
    }
};

}
