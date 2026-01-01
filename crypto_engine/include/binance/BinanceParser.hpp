// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceParser.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Zero-copy JSON parsing for Binance WebSocket messages
// OWNER: Jo
// LAST VERIFIED: 2024-12-24
//
// v6.99 FIX:
//   - CRITICAL BUG: @depth20@100ms uses DIFFERENT format than @depth@100ms!
//   - Partial Book Depth (@depth5/@depth10/@depth20) has NO "e" field!
//     Format: {"lastUpdateId":160,"bids":[...],"asks":[...]}
//   - Diff Depth (@depth/@depth@100ms) HAS "e":"depthUpdate"
//     Format: {"e":"depthUpdate","E":123,"s":"BTCUSDT",...}
//   - Combined stream wrapper: {"stream":"btcusdt@depth20@100ms","data":{...}}
//   - Must detect by stream name OR presence of lastUpdateId
//
// DESIGN:
// - No heap allocation during parsing
// - Direct string-to-number conversion without std::stod
// - Handles BOTH depth message formats
// - Returns views into original buffer (caller must keep buffer alive)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <array>

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// Message Types
// ─────────────────────────────────────────────────────────────────────────────
enum class MessageType : uint8_t {
    UNKNOWN           = 0,
    DEPTH_UPDATE      = 1,  // Either Partial Book or Diff Depth
    TRADE             = 2,
    BOOK_TICKER       = 3   // v7.12: Real-time best bid/ask
};

// ─────────────────────────────────────────────────────────────────────────────
// Price Level (bid or ask)
// ─────────────────────────────────────────────────────────────────────────────
struct PriceLevel {
    double price;
    double quantity;
};

// ─────────────────────────────────────────────────────────────────────────────
// Depth Update Message (works for BOTH formats)
// ─────────────────────────────────────────────────────────────────────────────
struct DepthUpdate {
    uint64_t event_time;        // E (or 0 for partial book)
    uint64_t first_update_id;   // U (or 0 for partial book)
    uint64_t last_update_id;    // u or lastUpdateId
    
    std::array<PriceLevel, 20> bids;  // b or bids - up to 20 levels
    std::array<PriceLevel, 20> asks;  // a or asks - up to 20 levels
    uint8_t bid_count;
    uint8_t ask_count;
    
    // Symbol as string_view into original buffer (or internal buffer for partial)
    const char* symbol;
    size_t      symbol_len;
    
    // v6.99: Track if this is partial book (no event time)
    bool is_partial_book;
};

// ─────────────────────────────────────────────────────────────────────────────
// Trade Message
// ─────────────────────────────────────────────────────────────────────────────
struct TradeUpdate {
    uint64_t event_time;      // E
    uint64_t trade_id;        // t
    uint64_t trade_time;      // T
    double   price;           // p
    double   quantity;        // q
    bool     is_buyer_maker;  // m
    
    // Symbol as string_view into original buffer
    const char* symbol;
    size_t      symbol_len;
};

// ─────────────────────────────────────────────────────────────────────────────
// BookTicker Update (v7.12: Real-time best bid/ask)
// Format: {"u":12345,"s":"BTCUSDT","b":"87650.00","B":"1.5","a":"87651.00","A":"2.0"}
// ─────────────────────────────────────────────────────────────────────────────
struct BookTickerUpdate {
    uint64_t update_id;       // u
    double   best_bid;        // b (best bid price)
    double   best_bid_qty;    // B (best bid quantity)
    double   best_ask;        // a (best ask price)
    double   best_ask_qty;    // A (best ask quantity)
    
    // Symbol as string_view into original buffer
    const char* symbol;
    size_t      symbol_len;
};

// ─────────────────────────────────────────────────────────────────────────────
// Fast Number Parsing (no locale, no allocation)
// ─────────────────────────────────────────────────────────────────────────────
namespace FastParse {

// Parse double from string (no allocation)
[[nodiscard]] inline double to_double(const char* str, size_t len) noexcept {
    if (!str || len == 0) return 0.0;
    
    double result = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;
    bool negative = false;
    bool in_fraction = false;
    size_t i = 0;
    
    if (str[0] == '-') {
        negative = true;
        ++i;
    }
    
    for (; i < len; ++i) {
        char c = str[i];
        if (c == '.') {
            in_fraction = true;
        } else if (c >= '0' && c <= '9') {
            if (in_fraction) {
                divisor *= 10.0;
                fraction += (c - '0') / divisor;
            } else {
                result = result * 10.0 + (c - '0');
            }
        } else if (c == '"' || c == '\0') {
            break;
        }
    }
    
    result += fraction;
    return negative ? -result : result;
}

// Parse uint64 from string
[[nodiscard]] inline uint64_t to_uint64(const char* str, size_t len) noexcept {
    if (!str || len == 0) return 0;
    
    uint64_t result = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
        } else if (c == '"' || c == ',' || c == '}' || c == '\0') {
            break;
        }
    }
    return result;
}

} // namespace FastParse

// ─────────────────────────────────────────────────────────────────────────────
// JSON Parser (minimal, non-allocating)
// ─────────────────────────────────────────────────────────────────────────────
class BinanceParser {
public:
    // Parse message and determine type
    // v6.99: Detects partial book depth by stream name or lastUpdateId
    // v7.12: Detects bookTicker by stream name or u/b/a fields
    [[nodiscard]] MessageType parse(const char* json, size_t len) noexcept {
        json_ = json;
        len_ = len;
        pos_ = 0;
        is_partial_book_ = false;
        is_book_ticker_ = false;
        stream_symbol_[0] = '\0';
        stream_symbol_len_ = 0;
        
        // Check for combined stream wrapper: {"stream":"xxx@depth20@100ms","data":{...}}
        if (find_key("stream")) {
            const char* stream_val = nullptr;
            size_t stream_len = 0;
            if (get_string_value(stream_val, stream_len)) {
                // v6.99: Extract symbol from stream name (before first @)
                // e.g., "btcusdt@depth20@100ms" -> "BTCUSDT"
                size_t at_pos = 0;
                while (at_pos < stream_len && stream_val[at_pos] != '@') ++at_pos;
                if (at_pos > 0 && at_pos < sizeof(stream_symbol_)) {
                    for (size_t i = 0; i < at_pos && i < sizeof(stream_symbol_) - 1; ++i) {
                        char c = stream_val[i];
                        stream_symbol_[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
                    }
                    stream_symbol_[at_pos] = '\0';
                    stream_symbol_len_ = at_pos;
                }
                
                // v6.99: Check if partial book depth stream (@depth5/@depth10/@depth20)
                for (size_t i = 0; i + 7 < stream_len; ++i) {
                    if (strncmp(stream_val + i, "@depth", 6) == 0) {
                        char next = stream_val[i + 6];
                        // @depth5, @depth10, @depth20 have digit after @depth
                        // @depth or @depth@100ms do NOT
                        if (next >= '0' && next <= '9') {
                            is_partial_book_ = true;
                            break;
                        }
                    }
                }
                
                // v7.12: Check if bookTicker stream
                for (size_t i = 0; i + 11 < stream_len; ++i) {
                    if (strncmp(stream_val + i, "@bookTicker", 11) == 0) {
                        is_book_ticker_ = true;
                        break;
                    }
                }
            }
            
            // Skip to data object
            if (!find_key("data")) {
                return MessageType::UNKNOWN;
            }
        }
        
        // v7.12: bookTicker has "u" (update_id) and "b"/"a" (best bid/ask) but NO "e"
        if (is_book_ticker_) {
            return MessageType::BOOK_TICKER;
        }
        
        // v6.99: For partial book depth, check for "lastUpdateId" (no "e" field!)
        size_t saved_pos = pos_;
        if (find_key("lastUpdateId")) {
            is_partial_book_ = true;
            pos_ = saved_pos;
            return MessageType::DEPTH_UPDATE;
        }
        pos_ = saved_pos;
        
        // v7.12: bookTicker can also be detected by having "u" but not "e" or "lastUpdateId"
        // Format: {"u":12345,"s":"BTCUSDT","b":"87650.00","B":"1.5","a":"87651.00","A":"2.0"}
        saved_pos = pos_;
        if (find_key("u")) {
            pos_ = saved_pos;
            // Check if it has "b" (best bid) - confirms bookTicker
            if (find_key("b")) {
                pos_ = saved_pos;
                return MessageType::BOOK_TICKER;
            }
        }
        pos_ = saved_pos;
        
        // Find event type (only in diff depth and trade messages)
        if (!find_key("e")) {
            if (is_partial_book_) {
                return MessageType::DEPTH_UPDATE;
            }
            return MessageType::UNKNOWN;
        }
        
        const char* event_type = nullptr;
        size_t event_len = 0;
        if (!get_string_value(event_type, event_len)) {
            return MessageType::UNKNOWN;
        }
        
        if (event_len == 11 && strncmp(event_type, "depthUpdate", 11) == 0) {
            return MessageType::DEPTH_UPDATE;
        } else if (event_len == 5 && strncmp(event_type, "trade", 5) == 0) {
            return MessageType::TRADE;
        }
        
        return MessageType::UNKNOWN;
    }
    
    // Parse depth update (handles BOTH partial book and diff depth)
    [[nodiscard]] bool parse_depth(DepthUpdate& out) noexcept {
        pos_ = 0;
        out.is_partial_book = is_partial_book_;
        
        // Skip to data if combined stream
        if (find_key("stream")) {
            if (!find_key("data")) return false;
        }
        
        size_t data_start = pos_;
        
        if (is_partial_book_) {
            // PARTIAL BOOK FORMAT: {"lastUpdateId":160,"bids":[...],"asks":[...]}
            out.event_time = 0;
            out.first_update_id = 0;
            
            pos_ = data_start;
            if (find_key("lastUpdateId")) {
                out.last_update_id = get_uint64_value();
            }
            
            // Symbol from stream name
            out.symbol = stream_symbol_;
            out.symbol_len = stream_symbol_len_;
            
            pos_ = data_start;
            out.bid_count = 0;
            if (find_key("bids")) {
                parse_price_levels(out.bids.data(), out.bid_count, 20);
            }
            
            pos_ = data_start;
            out.ask_count = 0;
            if (find_key("asks")) {
                parse_price_levels(out.asks.data(), out.ask_count, 20);
            }
        } else {
            // DIFF DEPTH FORMAT: {"e":"depthUpdate","E":123,"s":"BTCUSDT",...}
            pos_ = data_start;
            if (find_key("E")) {
                out.event_time = get_uint64_value();
            }
            
            pos_ = data_start;
            if (find_key("s")) {
                (void)get_string_value(out.symbol, out.symbol_len);
            }
            
            pos_ = data_start;
            if (find_key("U")) {
                out.first_update_id = get_uint64_value();
            }
            
            pos_ = data_start;
            if (find_key("u")) {
                out.last_update_id = get_uint64_value();
            }
            
            pos_ = data_start;
            out.bid_count = 0;
            if (find_key("b")) {
                parse_price_levels(out.bids.data(), out.bid_count, 20);
            }
            
            pos_ = data_start;
            out.ask_count = 0;
            if (find_key_exact("a")) {
                parse_price_levels(out.asks.data(), out.ask_count, 20);
            }
        }
        
        return out.bid_count > 0 || out.ask_count > 0;
    }
    
    // Parse trade
    [[nodiscard]] bool parse_trade(TradeUpdate& out) noexcept {
        pos_ = 0;
        
        if (find_key("stream")) {
            if (!find_key("data")) return false;
        }
        
        size_t data_start = pos_;
        
        pos_ = data_start;
        if (find_key("E")) {
            out.event_time = get_uint64_value();
        }
        
        pos_ = data_start;
        if (find_key("s")) {
            (void)get_string_value(out.symbol, out.symbol_len);
        }
        
        pos_ = data_start;
        if (find_key("t")) {
            out.trade_id = get_uint64_value();
        }
        
        pos_ = data_start;
        if (find_key("p")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.price = FastParse::to_double(val, val_len);
            }
        }
        
        pos_ = data_start;
        if (find_key("q")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.quantity = FastParse::to_double(val, val_len);
            }
        }
        
        pos_ = data_start;
        if (find_key("T")) {
            out.trade_time = get_uint64_value();
        }
        
        pos_ = data_start;
        if (find_key("m")) {
            out.is_buyer_maker = get_bool_value();
        }
        
        return true;
    }
    
    // v7.12: Parse bookTicker message for real-time best bid/ask
    // Format: {"u":12345,"s":"BTCUSDT","b":"87650.00","B":"1.5","a":"87651.00","A":"2.0"}
    [[nodiscard]] bool parse_book_ticker(BookTickerUpdate& out) noexcept {
        pos_ = 0;
        
        if (find_key("stream")) {
            if (!find_key("data")) return false;
        }
        
        size_t data_start = pos_;
        
        // Update ID
        pos_ = data_start;
        if (find_key("u")) {
            out.update_id = get_uint64_value();
        }
        
        // Symbol - try "s" first, fall back to stream_symbol_
        pos_ = data_start;
        if (find_key("s")) {
            (void)get_string_value(out.symbol, out.symbol_len);
        } else {
            out.symbol = stream_symbol_;
            out.symbol_len = stream_symbol_len_;
        }
        
        // Best bid price - NOTE: uses lowercase "b"
        pos_ = data_start;
        if (find_key_exact("b")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.best_bid = FastParse::to_double(val, val_len);
            }
        }
        
        // Best bid quantity - NOTE: uses uppercase "B"
        pos_ = data_start;
        if (find_key_exact("B")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.best_bid_qty = FastParse::to_double(val, val_len);
            }
        }
        
        // Best ask price - NOTE: uses lowercase "a"
        pos_ = data_start;
        if (find_key_exact("a")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.best_ask = FastParse::to_double(val, val_len);
            }
        }
        
        // Best ask quantity - NOTE: uses uppercase "A"
        pos_ = data_start;
        if (find_key_exact("A")) {
            const char* val;
            size_t val_len;
            if (get_string_value(val, val_len)) {
                out.best_ask_qty = FastParse::to_double(val, val_len);
            }
        }
        
        return out.best_bid > 0 && out.best_ask > 0;
    }

private:
    const char* json_;
    size_t      len_;
    size_t      pos_;
    bool        is_partial_book_;
    bool        is_book_ticker_;    // v7.12: true if parsing bookTicker message
    char        stream_symbol_[16];
    size_t      stream_symbol_len_;
    
    [[nodiscard]] bool find_key(const char* key) noexcept {
        size_t key_len = strlen(key);
        
        while (pos_ + key_len + 3 < len_) {
            if (json_[pos_] == '"') {
                if (strncmp(json_ + pos_ + 1, key, key_len) == 0 &&
                    json_[pos_ + 1 + key_len] == '"') {
                    pos_ += key_len + 2;
                    while (pos_ < len_ && (json_[pos_] == ':' || json_[pos_] == ' ')) {
                        ++pos_;
                    }
                    return true;
                }
            }
            ++pos_;
        }
        return false;
    }
    
    [[nodiscard]] bool find_key_exact(const char* key) noexcept {
        size_t key_len = strlen(key);
        
        while (pos_ + key_len + 3 < len_) {
            if (json_[pos_] == '"') {
                if (strncmp(json_ + pos_ + 1, key, key_len) == 0 &&
                    json_[pos_ + 1 + key_len] == '"' &&
                    json_[pos_ + 2 + key_len] == ':') {
                    pos_ += key_len + 3;
                    while (pos_ < len_ && json_[pos_] == ' ') ++pos_;
                    return true;
                }
            }
            ++pos_;
        }
        return false;
    }
    
    [[nodiscard]] bool get_string_value(const char*& val, size_t& val_len) noexcept {
        if (pos_ >= len_ || json_[pos_] != '"') return false;
        
        ++pos_;
        val = json_ + pos_;
        val_len = 0;
        
        while (pos_ + val_len < len_ && json_[pos_ + val_len] != '"') {
            ++val_len;
        }
        
        pos_ += val_len + 1;
        return val_len > 0;
    }
    
    [[nodiscard]] uint64_t get_uint64_value() noexcept {
        return FastParse::to_uint64(json_ + pos_, len_ - pos_);
    }
    
    [[nodiscard]] bool get_bool_value() noexcept {
        if (pos_ < len_) {
            return json_[pos_] == 't';
        }
        return false;
    }
    
    void parse_price_levels(PriceLevel* levels, uint8_t& count, uint8_t max_count) noexcept {
        count = 0;
        
        if (pos_ >= len_ || json_[pos_] != '[') return;
        ++pos_;
        
        while (pos_ < len_ && count < max_count) {
            while (pos_ < len_ && (json_[pos_] == ' ' || json_[pos_] == ',')) {
                ++pos_;
            }
            
            if (json_[pos_] == ']') break;
            
            if (json_[pos_] == '[') {
                ++pos_;
                
                const char* price_str;
                size_t price_len;
                if (get_string_value(price_str, price_len)) {
                    levels[count].price = FastParse::to_double(price_str, price_len);
                    
                    while (pos_ < len_ && json_[pos_] == ',') ++pos_;
                    
                    const char* qty_str;
                    size_t qty_len;
                    if (get_string_value(qty_str, qty_len)) {
                        levels[count].quantity = FastParse::to_double(qty_str, qty_len);
                        ++count;
                    }
                }
                
                while (pos_ < len_ && json_[pos_] != ']') ++pos_;
                if (pos_ < len_) ++pos_;
            } else {
                ++pos_;
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol ID Lookup
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] inline uint16_t symbol_to_id(const char* symbol, size_t len) noexcept {
    if (len >= 6) {
        if (symbol[0] == 'B' && symbol[1] == 'T' && symbol[2] == 'C') {
            return 1;  // BTCUSDT
        } else if (symbol[0] == 'E' && symbol[1] == 'T' && symbol[2] == 'H') {
            return 2;  // ETHUSDT
        } else if (symbol[0] == 'S' && symbol[1] == 'O' && symbol[2] == 'L') {
            return 3;  // SOLUSDT
        } else if (symbol[0] == 'A' && symbol[1] == 'V' && symbol[2] == 'A') {
            return 11; // AVAXUSDT
        } else if (symbol[0] == 'L' && symbol[1] == 'I' && symbol[2] == 'N') {
            return 12; // LINKUSDT
        } else if (symbol[0] == 'O' && symbol[1] == 'P' && symbol[2] == 'U') {
            return 13; // OPUSDT
        } else if (symbol[0] == 'A' && symbol[1] == 'R' && symbol[2] == 'B') {
            return 14; // ARBUSDT
        }
    }
    return 0;
}

} // namespace Binance
} // namespace Chimera
