#pragma once

#include <optional>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace chimera {
namespace allocation {

enum class EngineType {
    HFT,
    STRUCTURE
};

enum class MetalSymbol {
    XAUUSD,
    XAGUSD
};

enum class TradeSide {
    BUY,
    SELL,
    NONE
};

struct EngineIntent {
    bool valid = false;
    EngineType engine;
    MetalSymbol symbol;
    TradeSide side = TradeSide::NONE;
    double requested_quantity = 0.0;
    double confidence = 0.0;   // 0.0 – 1.0
    bool is_exit = false;
};

struct PositionState {
    bool active = false;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    double entry_price = 0.0;
    uint64_t entry_timestamp_ns = 0;
};

struct AllocatedIntent {
    bool valid = false;
    MetalSymbol symbol;
    TradeSide side = TradeSide::NONE;
    double quantity = 0.0;
    bool is_exit = false;
    EngineType source_engine;
    double confidence = 0.0;
};

struct AllocationConfig {
    double max_xau_exposure = 5.0;
    double max_xag_exposure = 3.0;
    double structure_min_confidence = 0.6;
    double structure_capital_base = 0.4;
    double structure_capital_boost = 0.5;
    double hft_capital_base = 0.8;
    double hft_capital_penalty = 0.5;
};

class EnhancedCapitalAllocator {
public:
    explicit EnhancedCapitalAllocator(const AllocationConfig& config = AllocationConfig{})
        : m_config(config)
    {
        reset_positions();
    }

    void set_max_symbol_exposure(MetalSymbol symbol, double max_quantity)
    {
        if (symbol == MetalSymbol::XAUUSD) 
            m_config.max_xau_exposure = max_quantity;
        else if (symbol == MetalSymbol::XAGUSD) 
            m_config.max_xag_exposure = max_quantity;
    }

    void update_position_state(MetalSymbol symbol, const PositionState& state)
    {
        m_positions[symbol] = state;
    }

    std::optional<AllocatedIntent> allocate(
        const EngineIntent& hft_intent,
        const EngineIntent& structure_intent)
    {
        // Priority 1: Exit orders always take precedence
        if (structure_intent.valid && structure_intent.is_exit)
            return build_exit_order(structure_intent);

        if (hft_intent.valid && hft_intent.is_exit)
            return build_exit_order(hft_intent);

        // Priority 2: Determine dominant engine
        const EngineType dominant = decide_dominant_engine(hft_intent, structure_intent);

        if (dominant == EngineType::STRUCTURE && structure_intent.valid)
            return process_structure_intent(structure_intent, hft_intent);

        if (dominant == EngineType::HFT && hft_intent.valid)
            return process_hft_intent(hft_intent, structure_intent);

        return std::nullopt;
    }

    const AllocationConfig& config() const { return m_config; }
    
    PositionState get_position(MetalSymbol symbol) const 
    {
        auto it = m_positions.find(symbol);
        if (it != m_positions.end())
            return it->second;
        return PositionState{};
    }

private:
    AllocationConfig m_config;
    std::unordered_map<MetalSymbol, PositionState> m_positions;

private:
    void reset_positions()
    {
        m_positions[MetalSymbol::XAUUSD] = PositionState{};
        m_positions[MetalSymbol::XAGUSD] = PositionState{};
    }

    double get_max_exposure(MetalSymbol symbol) const
    {
        return symbol == MetalSymbol::XAUUSD 
            ? m_config.max_xau_exposure 
            : m_config.max_xag_exposure;
    }

    PositionState get_current_position(MetalSymbol symbol) const
    {
        auto it = m_positions.find(symbol);
        if (it != m_positions.end())
            return it->second;
        return PositionState{};
    }

    std::optional<AllocatedIntent> build_exit_order(const EngineIntent& intent)
    {
        const PositionState current_pos = get_current_position(intent.symbol);
        if (!current_pos.active)
            return std::nullopt;

        return AllocatedIntent{
            true,
            intent.symbol,
            current_pos.side,
            0.0,
            true,
            intent.engine,
            1.0
        };
    }

    EngineType decide_dominant_engine(
        const EngineIntent& hft_intent,
        const EngineIntent& structure_intent)
    {
        if (!structure_intent.valid)
            return EngineType::HFT;

        if (!hft_intent.valid)
            return EngineType::STRUCTURE;

        // Structure engine wins when it has strong conviction
        if (structure_intent.confidence >= m_config.structure_min_confidence &&
            structure_intent.confidence >= hft_intent.confidence)
            return EngineType::STRUCTURE;

        return EngineType::HFT;
    }

    std::optional<AllocatedIntent> process_structure_intent(
        const EngineIntent& structure_intent,
        const EngineIntent& hft_intent)
    {
        const PositionState current_pos = get_current_position(structure_intent.symbol);

        const double capital_share = calculate_structure_capital_share(structure_intent.confidence);
        const double max_allowed = get_max_exposure(structure_intent.symbol) * capital_share;

        double final_quantity = std::min(structure_intent.requested_quantity, max_allowed);

        if (final_quantity <= 0.0)
            return std::nullopt;

        // Block opposing HFT signals when structure has strong conviction
        if (hft_intent.valid &&
            hft_intent.side != structure_intent.side &&
            structure_intent.confidence >= m_config.structure_min_confidence)
        {
            // Structure dominates - HFT signal blocked
        }

        return AllocatedIntent{
            true,
            structure_intent.symbol,
            structure_intent.side,
            final_quantity,
            false,
            EngineType::STRUCTURE,
            structure_intent.confidence
        };
    }

    std::optional<AllocatedIntent> process_hft_intent(
        const EngineIntent& hft_intent,
        const EngineIntent& structure_intent)
    {
        const PositionState current_pos = get_current_position(hft_intent.symbol);

        const double capital_share = calculate_hft_capital_share(
            structure_intent.valid ? structure_intent.confidence : 0.0
        );
        const double max_allowed = get_max_exposure(hft_intent.symbol) * capital_share;

        double final_quantity = std::min(hft_intent.requested_quantity, max_allowed);

        if (final_quantity <= 0.0)
            return std::nullopt;

        // Block HFT if structure is active and opposing
        if (structure_intent.valid &&
            structure_intent.side != hft_intent.side &&
            structure_intent.confidence >= m_config.structure_min_confidence)
        {
            return std::nullopt;
        }

        return AllocatedIntent{
            true,
            hft_intent.symbol,
            hft_intent.side,
            final_quantity,
            false,
            EngineType::HFT,
            hft_intent.confidence
        };
    }

    double calculate_structure_capital_share(double confidence) const
    {
        // Structure allocation grows with confidence
        const double share = m_config.structure_capital_base 
                           + confidence * m_config.structure_capital_boost;
        return std::clamp(share, m_config.structure_capital_base, 
                         m_config.structure_capital_base + m_config.structure_capital_boost);
    }

    double calculate_hft_capital_share(double structure_confidence) const
    {
        // HFT capital shrinks when structure is strong
        const double share = m_config.hft_capital_base 
                           - structure_confidence * m_config.hft_capital_penalty;
        return std::clamp(share, 0.2, m_config.hft_capital_base);
    }
};

} // namespace allocation
} // namespace chimera
