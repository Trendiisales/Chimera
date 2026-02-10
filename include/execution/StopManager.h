#pragma once

inline double tighten_stop(
    double current_stop,
    double entry_price,
    double impulse,
    bool impulse_decay
) {
    if (!impulse_decay) return current_stop;

    // Tighten by 15% of original stop distance
    double delta = (entry_price - current_stop) * 0.15;
    return current_stop + delta;
}
