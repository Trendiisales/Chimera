// =============================================================================
// MLTypes.hpp - Core ML Type Definitions for Chimera HFT
// =============================================================================
// PURPOSE: Define all ML-related data structures used across the system
// DESIGN: 
//   - MLFeatureRecord: 64-byte aligned for efficient binary logging
//   - MLDecision: ML inference output used by strategies
//   - All enums match MarketState.hpp for consistency
//
// THIS IS NOT:
//   - Price prediction
//   - Trade signal generation
//   - Autonomous trading logic
//
// THIS IS:
//   - Quality scoring of YOUR deterministic trades
//   - Sizing adjustment based on historical outcomes
//   - Risk governor sitting above deterministic logic
//
// v4.12.0: CRYPTO REMOVED - CFD only
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Chimera {
namespace ML {

// =============================================================================
// v4.12.0: Engine ID - Which top-level engine generated the trade
// =============================================================================
enum class EngineId : uint8_t {
    CFD     = 0,   // cTrader FIX/OpenAPI engine (Forex, Metals, Indices)
    INCOME  = 1,   // Income engine (NAS100 mean reversion)
    UNKNOWN = 255
};

// =============================================================================
// v4.12.0: Strategy ID - Which specific strategy/profile generated the trade
// =============================================================================
enum class StrategyId : uint8_t {
    // CFD strategies (10-19)
    PURE_SCALPER       = 10,  // Existing CFD scalper
    PREDATOR           = 11,  // Speed-based momentum
    OPEN_RANGE         = 12,  // Range breakout/fade
    VWAP_DEFENSE       = 13,  // Institutional VWAP defense
    STOP_RUN_FADE      = 14,  // Counter-trend on stop hunts
    SESSION_HANDOFF    = 15,  // Session transition plays
    LIQUIDITY_VACUUM   = 16,  // Fast liquidity vacuum
    
    // Income strategies (20-29)
    INCOME_MEAN_REV    = 20,  // NAS100 income window mean reversion
    
    UNKNOWN            = 255
};

// Helper to convert engine to string
inline const char* engineToString(EngineId e) {
    switch (e) {
        case EngineId::CFD:     return "CFD";
        case EngineId::INCOME:  return "INCOME";
        default:                return "UNKNOWN";
    }
}

// Helper to convert strategy to string
inline const char* strategyToString(StrategyId s) {
    switch (s) {
        case StrategyId::PURE_SCALPER:      return "PureScalper";
        case StrategyId::PREDATOR:          return "Predator";
        case StrategyId::OPEN_RANGE:        return "OpenRange";
        case StrategyId::VWAP_DEFENSE:      return "VwapDefense";
        case StrategyId::STOP_RUN_FADE:     return "StopRunFade";
        case StrategyId::SESSION_HANDOFF:   return "SessionHandoff";
        case StrategyId::LIQUIDITY_VACUUM:  return "LiquidityVacuum";
        case StrategyId::INCOME_MEAN_REV:   return "IncomeMeanRev";
        default:                            return "Unknown";
    }
}

// =============================================================================
// ML Market Regime (aligned with MarketState.hpp)
// =============================================================================
enum class MLRegime : uint8_t {
    LOW_VOL    = 0,
    NORMAL_VOL = 1,
    HIGH_VOL   = 2,
    CRISIS     = 3
};

// =============================================================================
// ML Market State (aligned with MarketState.hpp)
// =============================================================================
enum class MLMarketState : uint8_t {
    DEAD        = 0,
    TRENDING    = 1,
    RANGING     = 2,
    VOLATILE    = 3
};

// =============================================================================
// ML Trade Intent (aligned with MarketState.hpp)
// =============================================================================
enum class MLTradeIntent : uint8_t {
    NO_TRADE       = 0,
    MOMENTUM       = 1,
    MEAN_REVERSION = 2
};

// =============================================================================
// Feature Record - Binary logged per decision point (64 bytes)
// =============================================================================
// Logged twice:
//   1. BEFORE entry decision (realized_R = 0, mfe_R = 0, mae_R = 0)
//   2. ON trade close (with realized outcomes filled)
// =============================================================================
struct alignas(64) MLFeatureRecord {
    // ── Identification (12 bytes) ──
    uint64_t timestamp_ns;          // Exchange timestamp
    uint32_t symbol_id;             // Hashed symbol ID
    
    // ── State Classification (4 bytes) ──
    uint8_t state;                  // MLMarketState
    uint8_t intent;                 // MLTradeIntent
    uint8_t regime;                 // MLRegime
    int8_t  side;                   // +1 BUY, -1 SELL, 0 none
    
    // ── Microstructure Features (16 bytes) ──
    float ofi;                      // Order flow imbalance [-1, +1]
    float vpin;                     // Toxicity [0, 1]
    float spread_bps;               // Current spread in basis points
    float conviction_score;         // [0, 10] from MarketStateClassifier
    
    // ── Context (4 bytes) ──
    uint16_t minutes_from_open;     // Minutes since market open
    uint8_t  strategy_id;           // Which bucket strategy triggered (StrategyId)
    uint8_t  engine_id;             // Which engine (EngineId)
    
    // ── Outcomes (16 bytes) - Filled on trade close ──
    float realized_R;               // Realized R-multiple (PnL / risk)
    float mfe_R;                    // Max Favorable Excursion / risk
    float mae_R;                    // Max Adverse Excursion / risk
    uint32_t hold_time_ms;          // Time in trade (milliseconds)
    
    // ── Reserved (12 bytes) ──
    uint8_t reserved[12];
    
    // Total: 12 + 4 + 16 + 4 + 16 + 12 = 64 bytes exactly
    
    // Constructor
    MLFeatureRecord() noexcept {
        std::memset(this, 0, sizeof(MLFeatureRecord));
    }
    
    // Helpers
    bool hasOutcome() const noexcept { 
        return realized_R != 0.0f || mfe_R != 0.0f || mae_R != 0.0f; 
    }
    
    bool isWin() const noexcept { return realized_R > 0.0f; }
    bool isLoss() const noexcept { return realized_R < 0.0f; }
    
    MLMarketState getState() const noexcept { return static_cast<MLMarketState>(state); }
    MLTradeIntent getIntent() const noexcept { return static_cast<MLTradeIntent>(intent); }
    MLRegime getRegime() const noexcept { return static_cast<MLRegime>(regime); }
    EngineId getEngine() const noexcept { return static_cast<EngineId>(engine_id); }
    StrategyId getStrategy() const noexcept { return static_cast<StrategyId>(strategy_id); }
};

static_assert(sizeof(MLFeatureRecord) == 64, "MLFeatureRecord must be 64 bytes");
static_assert(alignof(MLFeatureRecord) == 64, "MLFeatureRecord must be cache-line aligned");

// =============================================================================
// ML Decision - Inference output from trained model
// =============================================================================
struct MLDecision {
    // ── Core Predictions ──
    float expected_R;               // Predicted R-multiple
    float prob_positive;            // P(R > 0)
    
    // ── Quantile Predictions (for asymmetric sizing) ──
    float q25;                      // 25th percentile expected outcome
    float q50;                      // Median expected outcome
    float q75;                      // 75th percentile expected outcome
    
    // ── Sizing Guidance ──
    float size_multiplier;          // [0.0, 2.0+] applied to base size
    bool allow_trade;               // False = skip this setup
    
    // ── Confidence ──
    float model_confidence;         // [0, 1] how confident is the model
    
    // ── Metadata ──
    MLRegime regime_used;           // Which regime model was used
    bool ml_active;                 // Was ML inference actually run
    
    // Constructor - Safe defaults
    MLDecision() noexcept 
        : expected_R(0.0f)
        , prob_positive(0.5f)
        , q25(0.0f)
        , q50(0.0f)
        , q75(0.0f)
        , size_multiplier(1.0f)
        , allow_trade(true)
        , model_confidence(0.5f)
        , regime_used(MLRegime::NORMAL_VOL)
        , ml_active(false)
    {}
    
    // Helpers
    bool shouldTrade() const noexcept { 
        return allow_trade && ml_active && expected_R > 0.1f; 
    }
    
    float getAdjustedSize(float base_size) const noexcept {
        if (!allow_trade) return 0.0f;
        return base_size * size_multiplier;
    }
    
    bool isAsymmetricOpportunity() const noexcept {
        // q75 >> |q25| means upside >> downside
        return (q75 > 1.0f) && (std::fabs(q25) < q75 * 0.5f);
    }
};

// =============================================================================
// Kelly Inputs - For capital-scaled sizing
// =============================================================================
struct KellyInputs {
    double prob_win;                // P(R > 0) from ML
    double expected_R;              // E[R] from ML
    double avg_loss_R;              // Average losing R (negative)
    double equity;                  // Current account equity
    double drawdown_pct;            // Current drawdown from peak
    double regime_mult;             // Regime-specific Kelly scale
    
    KellyInputs() noexcept 
        : prob_win(0.5)
        , expected_R(0.0)
        , avg_loss_R(-1.0)
        , equity(10000.0)
        , drawdown_pct(0.0)
        , regime_mult(1.0)
    {}
};

// =============================================================================
// Audit Record - Full decision chain for regulatory compliance
// =============================================================================
struct AuditRecord {
    uint64_t timestamp_ns;
    uint32_t symbol_id;
    uint16_t strategy_id;
    int8_t   side;                  // +1 BUY, -1 SELL
    uint8_t  padding;
    
    double price;
    double size;
    double stop;
    
    double ml_expected_R;
    double ml_prob;
    double kelly_frac;
    
    MLMarketState market_state;
    MLRegime regime;
    uint8_t padding2[6];
    
    double realized_R;              // Filled on close
    
    AuditRecord() noexcept {
        std::memset(this, 0, sizeof(AuditRecord));
    }
};

// =============================================================================
// Helper Functions
// =============================================================================

inline const char* regimeStr(MLRegime r) noexcept {
    switch (r) {
        case MLRegime::LOW_VOL:    return "LOW_VOL";
        case MLRegime::NORMAL_VOL: return "NORMAL_VOL";
        case MLRegime::HIGH_VOL:   return "HIGH_VOL";
        case MLRegime::CRISIS:     return "CRISIS";
        default: return "UNKNOWN";
    }
}

inline const char* stateStr(MLMarketState s) noexcept {
    switch (s) {
        case MLMarketState::DEAD:       return "DEAD";
        case MLMarketState::TRENDING:   return "TRENDING";
        case MLMarketState::RANGING:    return "RANGING";
        case MLMarketState::VOLATILE:   return "VOLATILE";
        default: return "UNKNOWN";
    }
}

inline const char* intentStr(MLTradeIntent i) noexcept {
    switch (i) {
        case MLTradeIntent::NO_TRADE:       return "NO_TRADE";
        case MLTradeIntent::MOMENTUM:       return "MOMENTUM";
        case MLTradeIntent::MEAN_REVERSION: return "MEAN_REVERSION";
        default: return "UNKNOWN";
    }
}

// Symbol ID hash function (deterministic, fast)
inline uint32_t symbolToId(const char* symbol) noexcept {
    uint32_t hash = 5381;
    while (*symbol) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(*symbol++);
    }
    return hash;
}

} // namespace ML
} // namespace Chimera
