#pragma once
#include <string>
#include "runtime/Context.hpp"

namespace chimera {

class ContextSnapshotter {
public:
    explicit ContextSnapshotter(Context& ctx);

    bool save(const std::string& path);
    bool load(const std::string& path);

private:
    Context& ctx_;
};

}
