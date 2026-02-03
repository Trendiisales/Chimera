#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <mutex>

namespace chimera {

class ExecutionThrottle {
public:
    ExecutionThrottle(uint32_t global_rate, uint32_t per_symbol_rate);

    bool allow_global();
    bool allow_symbol(const std::string& symbol);

private:
    uint64_t now_ns() const;

    uint32_t global_rate_;
    uint32_t per_symbol_rate_;
    uint32_t global_count_{0};
    uint64_t global_window_{0};

    std::unordered_map<std::string, uint32_t> sym_count_;
    std::unordered_map<std::string, uint64_t> sym_window_;
    std::mutex mtx_;
};

}
