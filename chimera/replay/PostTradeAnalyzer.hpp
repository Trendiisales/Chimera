#pragma once
#include <unordered_map>
#include "ReplayLog.hpp"
#include "PostTradeReport.hpp"

namespace chimera {

class PostTradeAnalyzer {
public:
    std::vector<PostTradeReport> analyze(const ReplayLog& log);
};

}
