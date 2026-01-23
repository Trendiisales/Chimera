#pragma once

namespace chimera {

class CapitalLadder {
public:
    CapitalLadder();

    void recordWin();
    void recordLoss();

    double sizeMultiplier() const;
    void applyDrawdown(double dd_bps);

private:
    int win_streak_;
    double multiplier_;
};

} // namespace chimera
