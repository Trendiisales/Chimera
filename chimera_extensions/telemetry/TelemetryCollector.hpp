#pragma once

#include <cstdint>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <deque>
#include <sstream>
#include <iomanip>

namespace chimera {
namespace telemetry {

enum class MetalSymbol {
    XAUUSD,
    XAGUSD
};

enum class TradeSide {
    BUY,
    SELL,
    NONE
};

enum class EngineType {
    HFT,
    STRUCTURE
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

struct MarketTickEvent {
    MetalSymbol symbol;
    double bid;
    double ask;
    double mid;
    double ofi;
    double spread;
    uint64_t timestamp_ns;
};

struct ExecutionEvent {
    MetalSymbol symbol;
    TradeSide side;
    double quantity;
    double fill_price;
    uint64_t send_timestamp_ns;
    uint64_t ack_timestamp_ns;
    uint64_t fill_timestamp_ns;
    bool is_close;
    bool filled;
};

struct EnginePerformanceStats {
    uint64_t total_trades = 0;
    uint64_t winning_trades = 0;
    uint64_t losing_trades = 0;
    double gross_pnl = 0.0;
    double net_pnl = 0.0;
    double max_drawdown = 0.0;
    double peak_equity = 0.0;
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double sharpe_ratio = 0.0;
    double win_rate = 0.0;
};

struct SymbolPerformanceStats {
    uint64_t total_trades = 0;
    uint64_t winning_trades = 0;
    uint64_t losing_trades = 0;
    double gross_pnl = 0.0;
    double net_pnl = 0.0;
    double max_drawdown = 0.0;
    double peak_equity = 0.0;
    double avg_slippage = 0.0;
    double avg_latency_ns = 0.0;
};

struct LatencyMetrics {
    double avg_send_to_ack_ns = 0.0;
    double avg_ack_to_fill_ns = 0.0;
    double avg_total_roundtrip_ns = 0.0;
    uint64_t sample_count = 0;
};

struct TelemetrySnapshot {
    std::unordered_map<EngineType, EnginePerformanceStats> engine_stats;
    std::unordered_map<MetalSymbol, SymbolPerformanceStats> symbol_stats;
    LatencyMetrics latency;
    double total_pnl = 0.0;
    double total_drawdown = 0.0;
    uint64_t total_trades = 0;
    uint64_t timestamp_ns = 0;
};

struct TelemetryConfig {
    size_t pnl_history_size = 1000;
    size_t latency_sample_size = 500;
};

class TelemetryCollector {
public:
    explicit TelemetryCollector(const TelemetryConfig& config = TelemetryConfig{})
        : m_config(config)
    {
        reset_stats();
    }

    void on_market_tick(const MarketTickEvent& tick)
    {
        m_last_quotes[tick.symbol] = tick.mid;
    }

    void on_execution(const ExecutionEvent& execution)
    {
        if (!execution.filled)
            return;

        // Calculate PnL
        const double pnl = calculate_execution_pnl(execution);

        // Update engine stats
        update_engine_stats(execution, pnl);

        // Update symbol stats
        update_symbol_stats(execution, pnl);

        // Update latency metrics
        update_latency_metrics(execution);

        // Track PnL history
        track_pnl_history(pnl);

        m_total_trades++;
    }

    void on_intent_approved(const AllocatedIntent& intent)
    {
        // Track intent approvals for future analysis
        m_approved_intents_count[intent.source_engine]++;
    }

    TelemetrySnapshot create_snapshot() const
    {
        TelemetrySnapshot snapshot;
        snapshot.engine_stats = m_engine_stats;
        snapshot.symbol_stats = m_symbol_stats;
        snapshot.latency = m_latency_metrics;
        snapshot.total_pnl = calculate_total_pnl();
        snapshot.total_drawdown = calculate_total_drawdown();
        snapshot.total_trades = m_total_trades;
        snapshot.timestamp_ns = get_current_timestamp_ns();
        
        return snapshot;
    }

    std::string generate_json_report() const
    {
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);
        json << "{\n";
        
        // Engine statistics
        json << "  \"engines\": {\n";
        for (const auto& [engine, stats] : m_engine_stats) {
            json << "    \"" << engine_type_to_string(engine) << "\": {\n";
            json << "      \"trades\": " << stats.total_trades << ",\n";
            json << "      \"wins\": " << stats.winning_trades << ",\n";
            json << "      \"losses\": " << stats.losing_trades << ",\n";
            json << "      \"pnl\": " << stats.net_pnl << ",\n";
            json << "      \"max_dd\": " << stats.max_drawdown << ",\n";
            json << "      \"win_rate\": " << stats.win_rate << "\n";
            json << "    },\n";
        }
        json << "  },\n";

        // Symbol statistics
        json << "  \"symbols\": {\n";
        for (const auto& [symbol, stats] : m_symbol_stats) {
            json << "    \"" << symbol_to_string(symbol) << "\": {\n";
            json << "      \"trades\": " << stats.total_trades << ",\n";
            json << "      \"pnl\": " << stats.net_pnl << ",\n";
            json << "      \"avg_slippage\": " << stats.avg_slippage << ",\n";
            json << "      \"avg_latency_ms\": " << (stats.avg_latency_ns / 1e6) << "\n";
            json << "    },\n";
        }
        json << "  },\n";

        // Latency metrics
        json << "  \"latency\": {\n";
        json << "    \"avg_roundtrip_ms\": " << (m_latency_metrics.avg_total_roundtrip_ns / 1e6) << ",\n";
        json << "    \"samples\": " << m_latency_metrics.sample_count << "\n";
        json << "  },\n";

        json << "  \"total_pnl\": " << calculate_total_pnl() << ",\n";
        json << "  \"total_trades\": " << m_total_trades << "\n";
        json << "}\n";

        return json.str();
    }

    const EnginePerformanceStats& get_engine_stats(EngineType engine) const
    {
        static EnginePerformanceStats empty;
        auto it = m_engine_stats.find(engine);
        if (it != m_engine_stats.end())
            return it->second;
        return empty;
    }

    const SymbolPerformanceStats& get_symbol_stats(MetalSymbol symbol) const
    {
        static SymbolPerformanceStats empty;
        auto it = m_symbol_stats.find(symbol);
        if (it != m_symbol_stats.end())
            return it->second;
        return empty;
    }

private:
    TelemetryConfig m_config;

    std::unordered_map<EngineType, EnginePerformanceStats> m_engine_stats;
    std::unordered_map<MetalSymbol, SymbolPerformanceStats> m_symbol_stats;
    std::unordered_map<EngineType, uint64_t> m_approved_intents_count;
    
    LatencyMetrics m_latency_metrics{};
    
    std::deque<double> m_pnl_history;
    std::unordered_map<MetalSymbol, double> m_last_quotes;
    
    uint64_t m_total_trades = 0;

private:
    void reset_stats()
    {
        m_engine_stats.clear();
        m_symbol_stats.clear();
        m_approved_intents_count.clear();
        m_pnl_history.clear();
        m_last_quotes.clear();
        m_total_trades = 0;
    }

    double calculate_execution_pnl(const ExecutionEvent& execution) const
    {
        // Simple PnL calculation - would need entry price tracking for real implementation
        return execution.fill_price * execution.quantity * 0.0001; // placeholder
    }

    void update_engine_stats(const ExecutionEvent& execution, double pnl)
    {
        // Determine source engine (would need to track this from intent approval)
        EngineType engine = EngineType::STRUCTURE; // Default assumption
        
        auto& stats = m_engine_stats[engine];
        stats.total_trades++;
        stats.gross_pnl += pnl;
        stats.net_pnl += pnl;

        if (pnl > 0) {
            stats.winning_trades++;
            stats.avg_win = (stats.avg_win * (stats.winning_trades - 1) + pnl) / stats.winning_trades;
        } else {
            stats.losing_trades++;
            stats.avg_loss = (stats.avg_loss * (stats.losing_trades - 1) + pnl) / stats.losing_trades;
        }

        update_drawdown(stats);
        update_win_rate(stats);
    }

    void update_symbol_stats(const ExecutionEvent& execution, double pnl)
    {
        auto& stats = m_symbol_stats[execution.symbol];
        stats.total_trades++;
        stats.gross_pnl += pnl;
        stats.net_pnl += pnl;

        if (pnl > 0)
            stats.winning_trades++;
        else
            stats.losing_trades++;

        // Calculate slippage
        const double expected_price = m_last_quotes[execution.symbol];
        const double slippage = std::abs(execution.fill_price - expected_price);
        stats.avg_slippage = (stats.avg_slippage * (stats.total_trades - 1) + slippage) / stats.total_trades;

        update_drawdown_symbol(stats);
    }

    void update_latency_metrics(const ExecutionEvent& execution)
    {
        const double send_to_ack = static_cast<double>(execution.ack_timestamp_ns - execution.send_timestamp_ns);
        const double ack_to_fill = static_cast<double>(execution.fill_timestamp_ns - execution.ack_timestamp_ns);
        const double total_roundtrip = static_cast<double>(execution.fill_timestamp_ns - execution.send_timestamp_ns);

        m_latency_metrics.sample_count++;
        
        m_latency_metrics.avg_send_to_ack_ns = 
            (m_latency_metrics.avg_send_to_ack_ns * (m_latency_metrics.sample_count - 1) + send_to_ack) 
            / m_latency_metrics.sample_count;
            
        m_latency_metrics.avg_ack_to_fill_ns = 
            (m_latency_metrics.avg_ack_to_fill_ns * (m_latency_metrics.sample_count - 1) + ack_to_fill) 
            / m_latency_metrics.sample_count;
            
        m_latency_metrics.avg_total_roundtrip_ns = 
            (m_latency_metrics.avg_total_roundtrip_ns * (m_latency_metrics.sample_count - 1) + total_roundtrip) 
            / m_latency_metrics.sample_count;
    }

    void track_pnl_history(double pnl)
    {
        m_pnl_history.push_back(pnl);
        if (m_pnl_history.size() > m_config.pnl_history_size)
            m_pnl_history.pop_front();
    }

    void update_drawdown(EnginePerformanceStats& stats)
    {
        stats.peak_equity = std::max(stats.peak_equity, stats.net_pnl);
        const double current_dd = stats.peak_equity - stats.net_pnl;
        stats.max_drawdown = std::max(stats.max_drawdown, current_dd);
    }

    void update_drawdown_symbol(SymbolPerformanceStats& stats)
    {
        stats.peak_equity = std::max(stats.peak_equity, stats.net_pnl);
        const double current_dd = stats.peak_equity - stats.net_pnl;
        stats.max_drawdown = std::max(stats.max_drawdown, current_dd);
    }

    void update_win_rate(EnginePerformanceStats& stats)
    {
        if (stats.total_trades > 0)
            stats.win_rate = static_cast<double>(stats.winning_trades) / stats.total_trades;
    }

    double calculate_total_pnl() const
    {
        double total = 0.0;
        for (const auto& [engine, stats] : m_engine_stats)
            total += stats.net_pnl;
        return total;
    }

    double calculate_total_drawdown() const
    {
        double max_dd = 0.0;
        for (const auto& [engine, stats] : m_engine_stats)
            max_dd = std::max(max_dd, stats.max_drawdown);
        return max_dd;
    }

    uint64_t get_current_timestamp_ns() const
    {
        // Would use actual system time in production
        return 0;
    }

    std::string engine_type_to_string(EngineType engine) const
    {
        switch (engine) {
            case EngineType::HFT: return "HFT";
            case EngineType::STRUCTURE: return "STRUCTURE";
            default: return "UNKNOWN";
        }
    }

    std::string symbol_to_string(MetalSymbol symbol) const
    {
        switch (symbol) {
            case MetalSymbol::XAUUSD: return "XAUUSD";
            case MetalSymbol::XAGUSD: return "XAGUSD";
            default: return "UNKNOWN";
        }
    }
};

} // namespace telemetry
} // namespace chimera
