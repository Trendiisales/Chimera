#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <optional>
#include <fstream>
#include "SPSCRingBuffer.hpp"

namespace chimera {
namespace spine {

// ==================== EVENT TYPE DEFINITIONS ====================

enum class EventType : uint16_t {
    TICK = 1,
    EXECUTION = 2,
    ORDER_INTENT = 3,
    RISK_UPDATE = 4,
    TELEMETRY = 5
};

enum class MetalSymbol : uint8_t {
    XAUUSD = 0,
    XAGUSD = 1
};

enum class TradeSide : uint8_t {
    BUY = 0,
    SELL = 1,
    NONE = 2
};

// ==================== EVENT ABI STRUCTURES ====================

struct EventHeader {
    uint64_t timestamp_ns;
    EventType type;
    uint16_t size;
    uint32_t sequence;
};

struct TickEventPayload {
    MetalSymbol symbol;
    uint8_t _padding[7];
    double bid;
    double ask;
    double mid;
    double ofi;
    double spread;
};

struct ExecutionEventPayload {
    MetalSymbol symbol;
    TradeSide side;
    uint8_t is_close;
    uint8_t filled;
    uint8_t _padding[4];
    double quantity;
    double fill_price;
    uint64_t send_timestamp_ns;
    uint64_t ack_timestamp_ns;
    uint64_t fill_timestamp_ns;
};

struct OrderIntentPayload {
    MetalSymbol symbol;
    TradeSide side;
    uint8_t is_exit;
    uint8_t source_engine;
    uint8_t _padding[4];
    double quantity;
    double confidence;
};

struct RiskUpdatePayload {
    double equity;
    double daily_pnl;
    double unrealized_pnl;
    int32_t consecutive_losses;
    float volatility_score;
};

// ==================== UNIFIED EVENT CONTAINER ====================

template<size_t MaxPayloadSize = 128>
struct Event {
    EventHeader header;
    uint8_t payload[MaxPayloadSize];

    template<typename T>
    void set_payload(const T& data)
    {
        static_assert(sizeof(T) <= MaxPayloadSize, "Payload too large");
        std::memcpy(payload, &data, sizeof(T));
        header.size = sizeof(T);
    }

    template<typename T>
    T get_payload() const
    {
        T data;
        std::memcpy(&data, payload, sizeof(T));
        return data;
    }
};

// ==================== BINARY JOURNAL ====================

class BinaryJournal {
public:
    explicit BinaryJournal(const std::string& filepath)
        : m_file(filepath, std::ios::binary | std::ios::out)
        , m_sequence(0)
    {
    }

    ~BinaryJournal()
    {
        if (m_file.is_open())
            m_file.close();
    }

    template<typename PayloadType>
    void write_event(EventType type, const PayloadType& payload, uint64_t timestamp_ns)
    {
        EventHeader header{
            timestamp_ns,
            type,
            static_cast<uint16_t>(sizeof(PayloadType)),
            m_sequence++
        };

        m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        m_file.write(reinterpret_cast<const char*>(&payload), sizeof(PayloadType));
    }

    void flush()
    {
        if (m_file.is_open())
            m_file.flush();
    }

private:
    std::ofstream m_file;
    uint32_t m_sequence;
};

// ==================== EVENT DISPATCHER ====================

template<size_t RingCapacity = 4096>
class EventDispatcher {
public:
    using EventBuffer = Event<128>;

    EventDispatcher()
        : m_sequence(0)
    {
    }

    bool dispatch_tick(const TickEventPayload& tick, uint64_t timestamp_ns)
    {
        EventBuffer event;
        event.header.timestamp_ns = timestamp_ns;
        event.header.type = EventType::TICK;
        event.header.sequence = m_sequence++;
        event.set_payload(tick);

        return m_event_queue.try_push(event);
    }

    bool dispatch_execution(const ExecutionEventPayload& execution, uint64_t timestamp_ns)
    {
        EventBuffer event;
        event.header.timestamp_ns = timestamp_ns;
        event.header.type = EventType::EXECUTION;
        event.header.sequence = m_sequence++;
        event.set_payload(execution);

        return m_event_queue.try_push(event);
    }

    bool dispatch_order(const OrderIntentPayload& order, uint64_t timestamp_ns)
    {
        EventBuffer event;
        event.header.timestamp_ns = timestamp_ns;
        event.header.type = EventType::ORDER_INTENT;
        event.header.sequence = m_sequence++;
        event.set_payload(order);

        return m_event_queue.try_push(event);
    }

    bool dispatch_risk_update(const RiskUpdatePayload& risk, uint64_t timestamp_ns)
    {
        EventBuffer event;
        event.header.timestamp_ns = timestamp_ns;
        event.header.type = EventType::RISK_UPDATE;
        event.header.sequence = m_sequence++;
        event.set_payload(risk);

        return m_event_queue.try_push(event);
    }

    std::optional<EventBuffer> poll_event()
    {
        EventBuffer event;
        if (m_event_queue.try_pop(event))
            return event;
        return std::nullopt;
    }

    size_t pending_count() const
    {
        return m_event_queue.size();
    }

    bool has_events() const
    {
        return !m_event_queue.is_empty();
    }

private:
    chimera::infra::SPSCRingBuffer<EventBuffer, RingCapacity> m_event_queue;
    std::atomic<uint32_t> m_sequence;
};

// ==================== REPLAY ENGINE ====================

class ReplayEngine {
public:
    explicit ReplayEngine(const std::string& journal_path)
        : m_file(journal_path, std::ios::binary | std::ios::in)
    {
    }

    ~ReplayEngine()
    {
        if (m_file.is_open())
            m_file.close();
    }

    template<typename EventHandler>
    bool replay_next(EventHandler& handler)
    {
        EventHeader header;
        if (!m_file.read(reinterpret_cast<char*>(&header), sizeof(header)))
            return false;

        if (header.type == EventType::TICK)
        {
            TickEventPayload tick;
            if (!m_file.read(reinterpret_cast<char*>(&tick), sizeof(tick)))
                return false;
            handler.on_tick(tick, header.timestamp_ns);
        }
        else if (header.type == EventType::EXECUTION)
        {
            ExecutionEventPayload exec;
            if (!m_file.read(reinterpret_cast<char*>(&exec), sizeof(exec)))
                return false;
            handler.on_execution(exec, header.timestamp_ns);
        }
        else if (header.type == EventType::ORDER_INTENT)
        {
            OrderIntentPayload order;
            if (!m_file.read(reinterpret_cast<char*>(&order), sizeof(order)))
                return false;
            handler.on_order(order, header.timestamp_ns);
        }
        else if (header.type == EventType::RISK_UPDATE)
        {
            RiskUpdatePayload risk;
            if (!m_file.read(reinterpret_cast<char*>(&risk), sizeof(risk)))
                return false;
            handler.on_risk_update(risk, header.timestamp_ns);
        }
        else
        {
            // Unknown event type - skip
            m_file.seekg(header.size, std::ios::cur);
        }

        return true;
    }

    template<typename EventHandler>
    void replay_all(EventHandler& handler)
    {
        while (replay_next(handler))
        {
            // Continue until end of file
        }
    }

    void seek_to_start()
    {
        m_file.clear();
        m_file.seekg(0, std::ios::beg);
    }

private:
    std::ifstream m_file;
};

// ==================== LATENCY ATTRIBUTION ====================

struct LatencyRecord {
    uint64_t event_timestamp_ns;
    uint64_t send_timestamp_ns;
    uint64_t ack_timestamp_ns;
    uint64_t fill_timestamp_ns;
    double quote_price;
    double fill_price;
    MetalSymbol symbol;
};

class LatencyAttributionEngine {
public:
    void record_quote(MetalSymbol symbol, double price, uint64_t timestamp_ns)
    {
        m_last_quotes[static_cast<size_t>(symbol)] = price;
    }

    void record_execution(const ExecutionEventPayload& execution)
    {
        LatencyRecord record;
        record.event_timestamp_ns = execution.send_timestamp_ns;
        record.send_timestamp_ns = execution.send_timestamp_ns;
        record.ack_timestamp_ns = execution.ack_timestamp_ns;
        record.fill_timestamp_ns = execution.fill_timestamp_ns;
        record.quote_price = m_last_quotes[static_cast<size_t>(execution.symbol)];
        record.fill_price = execution.fill_price;
        record.symbol = execution.symbol;

        update_metrics(record);
    }

    struct AggregateMetrics {
        double avg_send_to_ack_ns = 0.0;
        double avg_ack_to_fill_ns = 0.0;
        double avg_total_roundtrip_ns = 0.0;
        double avg_slippage = 0.0;
        uint64_t sample_count = 0;
    };

    AggregateMetrics get_metrics() const
    {
        return m_metrics;
    }

private:
    void update_metrics(const LatencyRecord& record)
    {
        const double send_to_ack = static_cast<double>(record.ack_timestamp_ns - record.send_timestamp_ns);
        const double ack_to_fill = static_cast<double>(record.fill_timestamp_ns - record.ack_timestamp_ns);
        const double total_roundtrip = static_cast<double>(record.fill_timestamp_ns - record.send_timestamp_ns);
        const double slippage = std::abs(record.fill_price - record.quote_price);

        m_metrics.sample_count++;
        
        const uint64_t n = m_metrics.sample_count;
        m_metrics.avg_send_to_ack_ns = (m_metrics.avg_send_to_ack_ns * (n - 1) + send_to_ack) / n;
        m_metrics.avg_ack_to_fill_ns = (m_metrics.avg_ack_to_fill_ns * (n - 1) + ack_to_fill) / n;
        m_metrics.avg_total_roundtrip_ns = (m_metrics.avg_total_roundtrip_ns * (n - 1) + total_roundtrip) / n;
        m_metrics.avg_slippage = (m_metrics.avg_slippage * (n - 1) + slippage) / n;
    }

    std::array<double, 2> m_last_quotes{};
    AggregateMetrics m_metrics{};
};

} // namespace spine
} // namespace chimera
