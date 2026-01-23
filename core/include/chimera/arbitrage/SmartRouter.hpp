#pragma once
#include "chimera/fitness/VenueFitness.hpp"
#include "chimera/venue/VenueExecutionIO.hpp"
#include <vector>

namespace chimera::arbitrage {

class SmartRouter {
public:
    SmartRouter(chimera::fitness::VenueFitness& f) : fitness(f) {}

    chimera::venue::VenueExecutionIO* best(
        const std::vector<chimera::venue::VenueExecutionIO*>& venues,
        const std::vector<std::string>& names
    ) {
        double best_score = -1;
        size_t idx = 0;

        for (size_t i = 0; i < names.size(); ++i) {
            double s = fitness.score(names[i]);
            if (s > best_score) {
                best_score = s;
                idx = i;
            }
        }

        return venues[idx];
    }

private:
    chimera::fitness::VenueFitness& fitness;
};

}
