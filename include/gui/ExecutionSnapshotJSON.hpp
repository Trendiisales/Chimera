#pragma once
#include <string>

namespace gui {
    struct ExecutionSnapshot;
    std::string EmitJSON(const ExecutionSnapshot& snap);
}
