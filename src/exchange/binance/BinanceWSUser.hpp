#pragma once
#include <string>
#include <atomic>
#include "runtime/Context.hpp"

namespace chimera {

class BinanceWSUser {
public:
    BinanceWSUser(Context& ctx, const std::string& rest_base);

    void run(std::atomic<bool>& running);

private:
    Context&    ctx_;
    std::string rest_base_;

    void connect_loop(std::atomic<bool>& running);
    void parse_message(const std::string& msg);
};

} // namespace chimera
