#pragma once

#include <libwebsockets.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>

class BinanceDepthWS {
public:
    // FIXED: Match main.cpp expected field names EXACTLY
    struct DepthUpdate {
        double best_bid;
        double best_ask;
        double best_bid_qty;
        double best_ask_qty;
        uint64_t first_update_id;
        uint64_t final_update_id;
        uint64_t timestamp_ms;
    };

    using UpdateCallback = std::function<void(const DepthUpdate&)>;

    BinanceDepthWS(const std::string& symbol, UpdateCallback cb);
    ~BinanceDepthWS();

    void start();
    void stop();
    bool is_connected() const { return connected_.load(); }
    
    uint64_t get_update_count() const { return update_count_.load(); }
    uint64_t get_resync_count() const { return resync_count_.load(); }

    int ws_callback(lws* wsi, lws_callback_reasons reason,
                    void* user, void* in, size_t len);

private:
    std::string symbol_;
    UpdateCallback callback_;
    
    lws_context* context_ = nullptr;
    lws_protocols protocols_[2];
    lws* wsi_ = nullptr;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> waiting_for_snapshot_{true};
    std::atomic<uint64_t> update_count_{0};
    std::atomic<uint64_t> resync_count_{0};
    
    uint64_t last_update_id_ = 0;
    uint64_t last_snapshot_ms_ = 0;
    uint64_t last_depth_rx_ms_ = 0;  // Track when last delta was applied
    
    std::string recv_buffer_;
    std::vector<char> send_buffer_;
    
    struct BookLevel {
        double price;
        double qty;
    };
    std::vector<BookLevel> bids_;
    std::vector<BookLevel> asks_;
    
    bool snapshot_received_ = false;
    
    void service_loop();
    void handle_message(const char* data, size_t len);
    void load_rest_snapshot();
    void process_snapshot(const std::string& json);
    void process_delta(const std::string& json);
    void apply_update(const std::string& json, uint64_t first_id, uint64_t final_id);
    void force_resync(const char* reason);
    
    bool parse_u64(const std::string& json, const char* key, uint64_t& out);
    bool parse_double(const std::string& json, const char* key, double& out);
    bool validate_book();
    
    friend int binance_depth_callback(lws* wsi, lws_callback_reasons reason,
                                      void* user, void* in, size_t len);
};
