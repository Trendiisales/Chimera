#include "binance_executor.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdlib>

using json = nlohmann::json;

static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t new_len = size * nmemb;
    s->append(static_cast<char*>(contents), new_len);
    return new_len;
}

BinanceExecutor::BinanceExecutor() {
    const char* key = std::getenv("BINANCE_API_KEY");
    const char* secret = std::getenv("BINANCE_API_SECRET");
    const char* mode_env = std::getenv("CHIMERA_MODE");
    
    if (key) api_key_ = key;
    if (secret) api_secret_ = secret;
    
    if (mode_env && std::string(mode_env) == "LIVE") {
        mode_ = ExecMode::LIVE;
        base_url_ = "https://fapi.binance.com";
    } else {
        mode_ = ExecMode::SHADOW;
        base_url_ = "https://testnet.binancefuture.com";
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

BinanceExecutor::~BinanceExecutor() {
    stop();
    curl_global_cleanup();
}

void BinanceExecutor::start() {
    running_.store(true);
    std::cout << "[EXEC] Started in " << (mode_ == ExecMode::LIVE ? "LIVE" : "SHADOW") << " mode\n";
}

void BinanceExecutor::stop() {
    running_.store(false);
}

void BinanceExecutor::setMode(ExecMode mode) {
    mode_ = mode;
    if (mode == ExecMode::LIVE) {
        base_url_ = "https://fapi.binance.com";
    } else {
        base_url_ = "https://testnet.binancefuture.com";
    }
}

ExecMode BinanceExecutor::mode() const {
    return mode_;
}

void BinanceExecutor::onFill(FillHandler h) {
    std::lock_guard<std::mutex> lock(handler_mtx_);
    fill_handler_ = std::move(h);
}

void BinanceExecutor::placeMarket(
    const std::string& symbol,
    Side side,
    double size,
    bool reduce_only,
    double ref_price,
    double spread_bps
) {
    if (!running_.load()) return;
    
    if (!risk_.allowTrade(symbol, side, size, ref_price)) {
        std::cout << "[EXEC] Risk rejected: " << symbol << " " 
                  << sideStr(side) << " " << size << "\n";
        return;
    }
    
    if (mode_ == ExecMode::SHADOW) {
        shadowFill(symbol, side, size, ref_price, spread_bps);
    } else {
        liveFill(symbol, side, size, reduce_only);
    }
}

void BinanceExecutor::shadowFill(
    const std::string& symbol,
    Side side,
    double size,
    double ref_price,
    double spread_bps
) {
    double slippage_bps = spread_bps * 0.5 + 0.5;
    double slippage_mult = 1.0;
    
    if (side == Side::BUY) {
        slippage_mult = 1.0 + slippage_bps / 10000.0;
    } else {
        slippage_mult = 1.0 - slippage_bps / 10000.0;
    }
    
    double fill_price = ref_price * slippage_mult;
    double commission = fill_price * size * 0.0004;
    
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
    
    risk_.onFill(symbol, side, size, fill_price);
    
    Fill f;
    f.symbol = symbol;
    f.side = side;
    f.size = size;
    f.price = fill_price;
    f.commission = commission;
    f.ts_ns = ts_ns;
    f.is_shadow = true;
    
    std::cout << "[SHADOW] " << symbol << " " << sideStr(side) 
              << " " << size << " @ " << fill_price 
              << " (slip: " << slippage_bps << "bps)\n";
    
    std::lock_guard<std::mutex> lock(handler_mtx_);
    if (fill_handler_) fill_handler_(f);
}

void BinanceExecutor::liveFill(
    const std::string& symbol,
    Side side,
    double size,
    bool reduce_only
) {
    if (api_key_.empty() || api_secret_.empty()) {
        std::cerr << "[EXEC] Missing API credentials\n";
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4);
    
    std::ostringstream query;
    query << "symbol=" << symbol
          << "&side=" << (side == Side::BUY ? "BUY" : "SELL")
          << "&type=MARKET"
          << "&quantity=" << size
          << "&timestamp=" << ms;
    
    if (reduce_only) {
        query << "&reduceOnly=true";
    }
    
    std::string query_str = query.str();
    std::string signature = sign(query_str);
    query_str += "&signature=" + signature;
    
    std::string url = base_url_ + "/fapi/v1/order";
    std::string response = httpPost(url, query_str);
    
    try {
        auto j = json::parse(response);
        
        if (j.contains("orderId")) {
            double fill_price = std::stod(j["avgPrice"].get<std::string>());
            double fill_qty = std::stod(j["executedQty"].get<std::string>());
            
            risk_.onFill(symbol, side, fill_qty, fill_price);
            
            auto ts = std::chrono::high_resolution_clock::now();
            uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                ts.time_since_epoch()
            ).count();
            
            Fill f;
            f.symbol = symbol;
            f.side = side;
            f.size = fill_qty;
            f.price = fill_price;
            f.commission = 0.0;
            f.ts_ns = ts_ns;
            f.is_shadow = false;
            
            std::cout << "[LIVE] " << symbol << " " << sideStr(side)
                      << " " << fill_qty << " @ " << fill_price << "\n";
            
            std::lock_guard<std::mutex> lock(handler_mtx_);
            if (fill_handler_) fill_handler_(f);
        } else {
            std::cerr << "[EXEC] Order failed: " << response << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[EXEC] Parse error: " << e.what() << "\n";
    }
}

std::string BinanceExecutor::sign(const std::string& query) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    
    HMAC(
        EVP_sha256(),
        api_secret_.c_str(),
        static_cast<int>(api_secret_.size()),
        reinterpret_cast<const unsigned char*>(query.c_str()),
        query.size(),
        digest,
        &len
    );
    
    std::stringstream ss;
    for (unsigned int i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

std::string BinanceExecutor::httpPost(const std::string& url, const std::string& body) {
    std::string response;
    
    CURL* curl = curl_easy_init();
    if (!curl) return response;
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-MBX-APIKEY: " + api_key_).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[EXEC] curl error: " << curl_easy_strerror(res) << "\n";
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return response;
}
