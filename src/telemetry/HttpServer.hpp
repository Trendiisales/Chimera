#pragma once
#include <cstdint>
#include "runtime/Context.hpp"

namespace chimera {

class HttpServer {
public:
    HttpServer(uint16_t port, Context& ctx);
    void run();   // reads ctx_.running — no separate flag needed

private:
    uint16_t port_;
    Context& ctx_;
};

}
