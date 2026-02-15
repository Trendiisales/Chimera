#pragma once
#include <vector>
#include <algorithm>
#include "../analytics/ExpectancyManager.hpp"
#include "V2Proposal.hpp"

namespace ChimeraV2 {

class Supervisor {
public:
    Supervisor(ExpectancyManager* exp)
        : expectancy_(exp) {}

    V2Proposal select(std::vector<V2Proposal>& proposals) {

        V2Proposal best;
        double best_score = -1e9;

        for (auto& p : proposals) {

            if (!p.valid)
                continue;

            if (expectancy_->throttle(p.engine_id))
                continue;

            double weight = expectancy_->engine_weight(p.engine_id);
            double adjusted = p.structural_score * weight;

            if (adjusted > best_score) {
                best = p;
                best_score = adjusted;
            }
        }

        return best;
    }

private:
    ExpectancyManager* expectancy_;
};

}
