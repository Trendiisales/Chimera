#include "BinanceDepthWS.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <cstring>
#include <cmath>
#include <curl/curl.h>

using json = nlohmann::json;

// Curl callback for receiving data
static size_t binance_curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// FIXED: Static callback bridge (now friend function)
int binance_depth_callback(lws* wsi, lws_callback_reasons reason,
                           void* user, void* in, size_t len) {
    auto* depth_ws = static_cast<BinanceDepthWS*>(lws_context_user(lws_get_context(wsi)));
    if (depth_ws) {
        return depth_ws->ws_callback(wsi, reason, user, in, len);
    }
    return 0;
}

BinanceDepthWS::BinanceDepthWS(const std::string& symbol, UpdateCallback cb)
    : symbol_(symbol), callback_(std::move(cb)) {
    
    protocols_[0] = {
        "binance-depth",
        binance_depth_callback,
        0,
        4096,
        0, nullptr, 0
    };
    protocols_[1] = {nullptr, nullptr, 0, 0, 0, nullptr, 0};
}

BinanceDepthWS::~BinanceDepthWS() {
    stop();
}

void BinanceDepthWS::start() {
    running_.store(true);
    std::thread([this]() { service_loop(); }).detach();
}

void BinanceDepthWS::stop() {
    running_.store(false);
    if (context_) {
        lws_cancel_service(context_);
    }
}

void BinanceDepthWS::service_loop() {
    lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.user = this;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context_ = lws_create_context(&info);
    if (!context_) {
        std::cerr << "[DEPTH_WS] ERROR: Failed to create context\n";
        return;
    }
    
    // CRITICAL FIX: Store path string so .c_str() remains valid
    std::string ws_path = "/ws/" + symbol_ + "@depth@100ms";
    
    lws_client_connect_info ccinfo{};
    memset(&ccinfo, 0, sizeof(ccinfo));
    
    ccinfo.context = context_;
    ccinfo.address = "stream.binance.com";
    ccinfo.port = 9443;  // FIXED: Binance depth requires 9443, not 443
    ccinfo.path = ws_path.c_str();
    ccinfo.host = "stream.binance.com";  // FIXED: Explicit host header required
    ccinfo.origin = "https://stream.binance.com";  // FIXED: Explicit origin required
    ccinfo.protocol = NULL;  // TEST: Use first protocol in table
    // FIXED: Both USE_SSL and ALLOW_SELFSIGNED required for Binance
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED;
    
    wsi_ = lws_client_connect_via_info(&ccinfo);
    if (!wsi_) {
        std::cerr << "[DEPTH_WS] ERROR: Failed to connect\n";
        lws_context_destroy(context_);
        return;
    }
    
    while (running_.load()) {
        lws_service(context_, 50);
        
        // Watchdog: force resync if NO DEPTH DATA for >5s
        if (snapshot_received_ && !waiting_for_snapshot_.load()) {
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            // FIXED: Check when last delta was applied, not when snapshot was loaded
            if (now - last_depth_rx_ms_ > 5000) {
                char reason[128];
                snprintf(reason, sizeof(reason), "No depth updates for %lums", now - last_depth_rx_ms_);
                force_resync(reason);
            }
        }
    }
    
    lws_context_destroy(context_);
}

// FIXED: Now a proper member function (not static)
int BinanceDepthWS::ws_callback(lws* wsi, lws_callback_reasons reason,
                                void* user, void* in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            std::cout << "[DEPTH_WS] ✅ Connected to /ws/" << symbol_ << "@depth@100ms\n";
            connected_.store(true);
            waiting_for_snapshot_.store(true);
            snapshot_received_ = false;
            last_update_id_ = 0;
            
            // CRITICAL: Load REST snapshot to establish lastUpdateId baseline
            std::cout << "[DEPTH] Loading REST snapshot...\n";
            std::thread([this]() { load_rest_snapshot(); }).detach();
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            handle_message(static_cast<const char*>(in), len);
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            std::cerr << "[DEPTH_WS] Connection lost - reconnecting...\n";
            connected_.store(false);
            waiting_for_snapshot_.store(true);
            snapshot_received_ = false;
            break;
            
        default:
            break;
    }
    return 0;
}

void BinanceDepthWS::handle_message(const char* data, size_t len) {
    recv_buffer_.append(data, len);
    
    // Check if complete message
    if (recv_buffer_.find('}') == std::string::npos) {
        return;
    }
    
    try {
        json j = json::parse(recv_buffer_);
        recv_buffer_.clear();
        
        // Snapshot response
        if (j.contains("lastUpdateId") && j.contains("bids") && j.contains("asks")) {
            process_snapshot(j.dump());
        }
        // Delta update
        else if (j.contains("U") && j.contains("u")) {
            process_delta(j.dump());
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[DEPTH_WS] JSON parse error: " << e.what() << "\n";
        recv_buffer_.clear();
        force_resync("JSON parse failure");
    }
}

void BinanceDepthWS::process_snapshot(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        uint64_t last_update_id;
        if (!parse_u64(json_str, "lastUpdateId", last_update_id)) {
            force_resync("Snapshot parse failure");
            return;
        }
        
        // Clear book
        bids_.clear();
        asks_.clear();
        
        // Parse bids
        if (j.contains("bids") && j["bids"].is_array()) {
            for (const auto& bid : j["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    BookLevel level;
                    level.price = std::stod(bid[0].get<std::string>());
                    level.qty = std::stod(bid[1].get<std::string>());
                    if (level.qty > 0) {
                        bids_.push_back(level);
                    }
                }
            }
        }
        
        // Parse asks
        if (j.contains("asks") && j["asks"].is_array()) {
            for (const auto& ask : j["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    BookLevel level;
                    level.price = std::stod(ask[0].get<std::string>());
                    level.qty = std::stod(ask[1].get<std::string>());
                    if (level.qty > 0) {
                        asks_.push_back(level);
                    }
                }
            }
        }
        
        // Validate book
        if (!validate_book()) {
            force_resync("Snapshot validation failed");
            return;
        }
        
        // Update state
        last_update_id_ = last_update_id;
        last_snapshot_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        last_depth_rx_ms_ = last_snapshot_ms_;  // Initialize depth RX timer
        snapshot_received_ = true;
        waiting_for_snapshot_.store(false);
        
        std::cout << "[DEPTH] Snapshot loaded (lastUpdateId=" << last_update_id << ", "
                  << bids_.size() << " bids, " << asks_.size() << " asks)\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[DEPTH_WS] Snapshot error: " << e.what() << "\n";
        force_resync("Snapshot exception");
    }
}

void BinanceDepthWS::load_rest_snapshot() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[DEPTH] Failed to init curl\n";
        return;
    }
    
    std::string response;
    std::string url = "https://api.binance.com/api/v3/depth?symbol=" + symbol_ + "&limit=1000";
    
    // Convert to uppercase for API
    std::string upper_symbol = symbol_;
    for (auto& c : upper_symbol) c = std::toupper(c);
    url = "https://api.binance.com/api/v3/depth?symbol=" + upper_symbol + "&limit=1000";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, binance_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "[DEPTH] REST snapshot failed: " << curl_easy_strerror(res) << "\n";
        return;
    }
    
    std::cout << "[DEPTH] REST snapshot downloaded (" << response.size() << " bytes)\n";
    
    // Process as snapshot
    process_snapshot(response);
}

void BinanceDepthWS::process_delta(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        uint64_t U, u;
        if (!parse_u64(json_str, "\"U\"", U) || !parse_u64(json_str, "\"u\"", u)) {
            // Silently skip - not a delta update
            return;
        }
        
        // ========================================
        // CRITICAL: BINANCE SNAPSHOT/DELTA BRIDGE
        // ========================================
        // Must load REST snapshot FIRST, then wait for:
        // U <= lastUpdateId+1 AND u >= lastUpdateId+1
        // Only then start processing deltas
        // ========================================
        
        // If waiting for snapshot bridge frame
        if (waiting_for_snapshot_.load()) {
            // Check if this is the bridge frame
            if (U <= last_update_id_ + 1 && u >= last_update_id_ + 1) {
                waiting_for_snapshot_.store(false);
                std::cout << "[DEPTH] Book ARMED at U=" << U << " u=" << u 
                          << " (lastUpdateId=" << last_update_id_ << ")\n";
                // Fall through to apply this frame
            } else {
                // Still waiting for correct bridge frame
                return;
            }
        }
        
        // ========================================
        // GAP DETECTION - FORCE RESYNC
        // ========================================
        if (U != last_update_id_ + 1) {
            char reason[256];
            snprintf(reason, sizeof(reason), 
                     "Sequence gap: expected U=%lu got U=%lu", last_update_id_ + 1, U);
            force_resync(reason);
            return;
        }
        
        // Valid update - apply it
        apply_update(json_str, U, u);
        
        // Update sequence
        last_update_id_ = u;
        
        // CRITICAL: Update depth RX timer to prevent false stale detection
        last_depth_rx_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
    } catch (const std::exception& e) {
        std::cerr << "[DEPTH_WS] Delta error: " << e.what() << "\n";
        force_resync("Delta exception");
    }
}

void BinanceDepthWS::apply_update(const std::string& json_str, uint64_t first_id, uint64_t final_id) {
    try {
        json j = json::parse(json_str);
        
        // Apply bid updates
        if (j.contains("b") && j["b"].is_array()) {
            for (const auto& bid : j["b"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    double price = std::stod(bid[0].get<std::string>());
                    double qty = std::stod(bid[1].get<std::string>());
                    
                    // Remove if qty = 0
                    if (qty == 0.0) {
                        bids_.erase(std::remove_if(bids_.begin(), bids_.end(),
                            [price](const BookLevel& l) { return l.price == price; }), bids_.end());
                    } else {
                        // Update or insert
                        bool found = false;
                        for (auto& level : bids_) {
                            if (level.price == price) {
                                level.qty = qty;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            bids_.push_back({price, qty});
                        }
                    }
                }
            }
        }
        
        // Apply ask updates
        if (j.contains("a") && j["a"].is_array()) {
            for (const auto& ask : j["a"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    double price = std::stod(ask[0].get<std::string>());
                    double qty = std::stod(ask[1].get<std::string>());
                    
                    // Remove if qty = 0
                    if (qty == 0.0) {
                        asks_.erase(std::remove_if(asks_.begin(), asks_.end(),
                            [price](const BookLevel& l) { return l.price == price; }), asks_.end());
                    } else {
                        // Update or insert
                        bool found = false;
                        for (auto& level : asks_) {
                            if (level.price == price) {
                                level.qty = qty;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            asks_.push_back({price, qty});
                        }
                    }
                }
            }
        }
        
        // Sort (bids desc, asks asc)
        std::sort(bids_.begin(), bids_.end(), 
                 [](const BookLevel& a, const BookLevel& b) { return a.price > b.price; });
        std::sort(asks_.begin(), asks_.end(), 
                 [](const BookLevel& a, const BookLevel& b) { return a.price < b.price; });
        
        // Validate book after update
        if (!validate_book()) {
            force_resync("Post-update validation failed");
            return;
        }
        
        // FIXED: Use DepthUpdate type name with correct field names
        if (!bids_.empty() && !asks_.empty()) {
            DepthUpdate update;
            update.best_bid = bids_[0].price;
            update.best_ask = asks_[0].price;
            update.best_bid_qty = bids_[0].qty;
            update.best_ask_qty = asks_[0].qty;
            update.first_update_id = first_id;
            update.final_update_id = final_id;
            update.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            callback_(update);
            update_count_.fetch_add(1);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[DEPTH_WS] Apply error: " << e.what() << "\n";
        force_resync("Apply exception");
    }
}

bool BinanceDepthWS::validate_book() {
    if (bids_.empty() || asks_.empty()) {
        std::cerr << "[DEPTH_WS] INVALID BOOK: Empty sides\n";
        return false;
    }
    
    double best_bid = bids_[0].price;
    double best_ask = asks_[0].price;
    
    // Check for crossed book
    if (best_bid >= best_ask) {
        std::cerr << "[DEPTH_WS] CROSSED BOOK: bid=" << best_bid 
                  << " ask=" << best_ask << "\n";
        return false;
    }
    
    // Check spread sanity (0-50 bps)
    double spread_bps = ((best_ask - best_bid) / best_bid) * 10000.0;
    if (spread_bps < 0 || spread_bps > 50.0) {
        std::cerr << "[DEPTH_WS] INVALID SPREAD: " << spread_bps << "bps\n";
        return false;
    }
    
    return true;
}

void BinanceDepthWS::force_resync(const char* reason) {
    std::cerr << "[DEPTH_WS] FORCE RESYNC: " << reason << "\n";
    
    // Clear state
    bids_.clear();
    asks_.clear();
    last_update_id_ = 0;
    last_depth_rx_ms_ = 0;  // Reset depth RX timer
    snapshot_received_ = false;
    waiting_for_snapshot_.store(true);
    recv_buffer_.clear();
    resync_count_.fetch_add(1);
    
    // Reload REST snapshot
    std::cout << "[DEPTH] Reloading REST snapshot after resync...\n";
    std::thread([this]() { load_rest_snapshot(); }).detach();
}

bool BinanceDepthWS::parse_u64(const std::string& json, const char* key, uint64_t& out) {
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    pos++; // Skip ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    
    size_t end = pos;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') end++;
    
    if (end == pos) return false;
    
    try {
        out = std::stoull(json.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}

bool BinanceDepthWS::parse_double(const std::string& json, const char* key, double& out) {
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    pos++; // Skip ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    
    size_t end = pos;
    while (end < json.size() && (json[end] == '.' || json[end] == '-' || 
           (json[end] >= '0' && json[end] <= '9'))) end++;
    
    if (end == pos) return false;
    
    try {
        out = std::stod(json.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}
