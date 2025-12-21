#pragma once
#include <string>
#include <array>

namespace Chimera {

enum class Regime {
    TREND,
    MEAN,
    HIGH_VOL,
    LOW_VOL,
    NOISY
};

struct StrategySpec {
    const char* name;
    Regime allowed[2];   // coarse gate
    bool always_on;
};

constexpr std::array<StrategySpec,10> STRATEGIES = {{
    {"Momentum_Trend",        {Regime::TREND,   Regime::HIGH_VOL}, false},
    {"MeanReversion",         {Regime::MEAN,    Regime::LOW_VOL},  false},
    {"Volatility_Expansion",  {Regime::HIGH_VOL,Regime::NOISY},    false},
    {"Volatility_Compression",{Regime::LOW_VOL, Regime::MEAN},     false},
    {"Liquidity_Vacuum",      {Regime::NOISY,   Regime::HIGH_VOL}, false},
    {"Orderflow_Imbalance",   {Regime::TREND,   Regime::NOISY},    false},
    {"Breakout_Session",      {Regime::TREND,   Regime::HIGH_VOL}, false},
    {"Fade_Extremes",         {Regime::MEAN,    Regime::LOW_VOL},  false},
    {"Range_Rotation",        {Regime::MEAN,    Regime::LOW_VOL},  false},
    {"NoTrade_Guard",         {Regime::TREND,   Regime::MEAN},     true }
}};

inline bool allowed_in(Regime r, const StrategySpec& s) {
    return s.always_on || (r == s.allowed[0] || r == s.allowed[1]);
}

} // namespace Chimera
