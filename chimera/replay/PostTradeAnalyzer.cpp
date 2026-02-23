#include "PostTradeAnalyzer.hpp"

namespace chimera {

std::vector<PostTradeReport> PostTradeAnalyzer::analyze(const ReplayLog& log) {
    std::unordered_map<uint64_t, PostTradeReport> reports;

    for (const ReplayEvent& e : log.events()) {
        PostTradeReport& r = reports[e.causal_id];
        r.causal_id = e.causal_id;

        switch (e.type) {
            case ReplayEventType::SIGNAL:
                r.entry_reason = e.payload_json;
                break;
            case ReplayEventType::FILL:
                r.exit_reason = "FILLED";
                break;
            default:
                break;
        }
    }

    std::vector<PostTradeReport> out;
    for (auto& kv : reports)
        out.push_back(kv.second);

    return out;
}

}
