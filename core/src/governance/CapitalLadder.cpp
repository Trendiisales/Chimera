#include "chimera/governance/CapitalLadder.hpp"

namespace chimera {

CapitalLadder::CapitalLadder()
    : win_streak_(0), multiplier_(1.0) {}

void CapitalLadder::recordWin() {
    win_streak_++;
    if (win_streak_ >= 3) {
        multiplier_ *= 1.25;
        win_streak_ = 0;
    }
}

void CapitalLadder::recordLoss() {
    win_streak_ = 0;
    multiplier_ *= 0.8;
    if (multiplier_ < 0.25) multiplier_ = 0.25;
}

double CapitalLadder::sizeMultiplier() const {
    return multiplier_;
}

void CapitalLadder::applyDrawdown(double dd_bps) {
    if (dd_bps > 50.0) {
        multiplier_ *= 0.5;
        if (multiplier_ < 0.25) multiplier_ = 0.25;
    }
}

} // namespace chimera
