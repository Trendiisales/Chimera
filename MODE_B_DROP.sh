#!/usr/bin/env bash
set -e

echo "[CHIMERA] MODE B DROP INTO ROOT"

# =====================
# DIRECTORIES
# =====================
mkdir -p exchange router account build

# =====================
# KEYS
# =====================
cat > keys.json << 'KEYS'
{
  "api_key": "PUT_KEY_HERE",
  "api_secret": "PUT_SECRET_HERE",
  "mode": "DRY"
}
KEYS

# =====================
# account/ApiKeys.hpp
# =====================
cat > account/ApiKeys.hpp << 'AK'
#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

struct ApiKeys {
    std::string api_key;
    std::string api_secret;
    bool dry_run = true;

    static ApiKeys load(const std::string& path) {
        ApiKeys k;
        std::ifstream f(path);
        if (!f.is_open()) throw std::runtime_error("Cannot open keys.json");

        std::stringstream ss;
        ss << f.rdbuf();
        std::string s = ss.str();

        auto get = [&](const std::string& key) {
            auto p = s.find(key);
            if (p == std::string::npos) return std::string();
            auto q = s.find('"', p + key.size() + 2);
            auto r = s.find('"', q + 1);
            return s.substr(q + 1, r - q - 1);
        };

        k.api_key = get("api_key");
        k.api_secret = get("api_secret");
        k.dry_run = get("mode") != "LIVE";
        return k;
    }
};
AK

# =====================
# account/PositionTracker.hpp
# =====================
cat > account/PositionTracker.hpp << 'PT'
#pragma once
#include <unordered_map>
#include <string>
#include <mutex>

struct Position {
    double qty = 0.0;
    double avg_price = 0.0;
    double realized_pnl = 0.0;
};

class PositionTracker {
    std::mutex mtx;
    std::unordered_map<std::string, Position> positions;

public:
    void onFill(const std::string& sym, double qty, double price);
    double unrealized(const std::string& sym, double mark);
    Position get(const std::string& sym);
};
PT

# =====================
# account/PositionTracker.cpp
# =====================
cat > account/PositionTracker.cpp << 'PTC'
#include "PositionTracker.hpp"
#include <cmath>

void PositionTracker::onFill(const std::string& sym, double qty, double price) {
    std::lock_guard<std::mutex> lock(mtx);
    auto& p = positions[sym];

    double new_qty = p.qty + qty;

    if (p.qty != 0 && (p.qty > 0) != (new_qty > 0)) {
        double closed = std::min(std::abs(qty), std::abs(p.qty));
        p.realized_pnl += closed * (price - p.avg_price) * (p.qty > 0 ? 1 : -1);
    }

    if (p.qty + qty != 0)
        p.avg_price = (p.avg_price * p.qty + price * qty) / (p.qty + qty);

    p.qty = new_qty;
}

double PositionTracker::unrealized(const std::string& sym, double mark) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = positions.find(sym);
    if (it == positions.end()) return 0;
    return it->second.qty * (mark - it->second.avg_price);
}

Position PositionTracker::get(const std::string& sym) {
    std::lock_guard<std::mutex> lock(mtx);
    return positions[sym];
}
PTC

# =====================
# exchange/BinanceREST.hpp
# =====================
cat > exchange/BinanceREST.hpp << 'BRH'
#pragma once
#include <string>

class BinanceREST {
    std::string api_key;
    std::string api_secret;
    bool dry;

    std::string sign(const std::string& query);

public:
    BinanceREST(const std::string& key, const std::string& secret, bool dry_run);

    std::string sendOrder(const std::string& symbol,
                          const std::string& side,
                          double qty,
                          double price = 0,
                          bool market = true);

    void cancelAll(const std::string& symbol);
};
BRH

# =====================
# exchange/BinanceREST.cpp
# =====================
cat > exchange/BinanceREST.cpp << 'BRC'
#include "BinanceREST.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <sstream>
#include <iomanip>
#include <ctime>

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* data) {
    ((std::string*)data)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

BinanceREST::BinanceREST(const std::string& k,
                         const std::string& s,
                         bool d)
    : api_key(k), api_secret(s), dry(d) {}

std::string BinanceREST::sign(const std::string& q) {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(),
                  api_secret.c_str(),
                  api_secret.size(),
                  (unsigned char*)q.c_str(),
                  q.size(),
                  nullptr,
                  nullptr);

    std::ostringstream ss;
    for (int i = 0; i < 32; i++)
        ss << std::hex << std::setw(2)
           << std::setfill('0')
           << (int)digest[i];
    return ss.str();
}

std::string BinanceREST::sendOrder(const std::string& symbol,
                                   const std::string& side,
                                   double qty,
                                   double price,
                                   bool market) {
    if (dry) return "DRY_RUN_OK";

    CURL* curl = curl_easy_init();
    std::string result;

    long ts = std::time(nullptr) * 1000;

    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=" << side
      << "&type=" << (market ? "MARKET" : "LIMIT")
      << "&quantity=" << qty
      << "&timestamp=" << ts;

    if (!market)
        q << "&price=" << price
          << "&timeInForce=GTC";

    std::string sig = sign(q.str());

    std::string url =
        "https://api.binance.com/api/v3/order?" +
        q.str() + "&signature=" + sig;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(
        headers,
        ("X-MBX-APIKEY: " + api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return result;
}

void BinanceREST::cancelAll(const std::string& symbol) {
    if (dry) return;

    CURL* curl = curl_easy_init();
    long ts = std::time(nullptr) * 1000;

    std::ostringstream q;
    q << "symbol=" << symbol
      << "&timestamp=" << ts;

    std::string sig = sign(q.str());

    std::string url =
        "https://api.binance.com/api/v3/openOrders?" +
        q.str() + "&signature=" + sig;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(
        headers,
        ("X-MBX-APIKEY: " + api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}
BRC

# =====================
# exchange/BinanceWSClient.hpp
# =====================
cat > exchange/BinanceWSClient.hpp << 'BWH'
#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>

class BinanceWSClient {
public:
    using TickCB = std::function<void(const std::string&, double, double)>;

private:
    std::string url;
    TickCB cb;
    std::thread worker;
    std::atomic<bool> running{false};

public:
    BinanceWSClient(const std::string& ws_url);
    void setCallback(TickCB f);
    void start();
    void stop();
};
BWH

# =====================
# exchange/BinanceWSClient.cpp
# =====================
cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
typedef websocketpp::client<websocketpp::config::asio_client> client;

BinanceWSClient::BinanceWSClient(const std::string& ws)
    : url(ws) {}

void BinanceWSClient::setCallback(TickCB f) {
    cb = f;
}

void BinanceWSClient::start() {
    running = true;
    worker = std::thread([&]() {
        client c;
        c.init_asio();

        c.set_message_handler([&](auto, auto msg) {
            auto j = json::parse(msg->get_payload());
            if (!cb) return;

            if (j.contains("s") &&
                j.contains("b") &&
                j.contains("a")) {

                std::string sym = j["s"];
                double bid = std::stod(j["b"].get<std::string>());
                double ask = std::stod(j["a"].get<std::string>());
                cb(sym, bid, ask);
            }
        });

        websocketpp::lib::error_code ec;
        auto con = c.get_connection(url, ec);
        c.connect(con);
        c.run();
    });
}

void BinanceWSClient::stop() {
    running = false;
    if (worker.joinable()) worker.join();
}
BWC

# =====================
# router/CapitalRouter.hpp
# =====================
cat > router/CapitalRouter.hpp << 'CRH'
#pragma once
#include "../risk/KillSwitchGovernor.hpp"
#include "../exchange/BinanceREST.hpp"
#include "../account/PositionTracker.hpp"

class CapitalRouter {
    KillSwitchGovernor* kill;
    BinanceREST* rest;
    PositionTracker* tracker;

public:
    CapitalRouter(KillSwitchGovernor* k,
                  BinanceREST* r,
                  PositionTracker* t);

    void send(const std::string& symbol,
              const std::string& side,
              double qty,
              double price = 0,
              bool market = true);
};
CRH

# =====================
# router/CapitalRouter.cpp
# =====================
cat > router/CapitalRouter.cpp << 'CRC'
#include "CapitalRouter.hpp"

CapitalRouter::CapitalRouter(KillSwitchGovernor* k,
                             BinanceREST* r,
                             PositionTracker* t)
    : kill(k), rest(r), tracker(t) {}

void CapitalRouter::send(const std::string& symbol,
                         const std::string& side,
                         double qty,
                         double price,
                         bool market) {
    if (!kill->canTrade()) return;

    auto res = rest->sendOrder(symbol, side, qty, price, market);

    if (res == "DRY_RUN_OK") {
        tracker->onFill(symbol,
                          side == "BUY" ? qty : -qty,
                          price == 0 ? 0.0 : price);
    }
}
CRC

# =====================
# main.cpp
# =====================
cat > main.cpp << 'MAIN'
#include "account/ApiKeys.hpp"
#include "account/PositionTracker.hpp"
#include "exchange/BinanceWSClient.hpp"
#include "exchange/BinanceREST.hpp"
#include "router/CapitalRouter.hpp"

#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "engines/FadeETH_WORKING.hpp"
#include "engines/CascadeBTC_WORKING.hpp"
#include "engines/FundingSniper.hpp"
#include "risk/KillSwitchGovernor.hpp"

#include <iostream>
#include <thread>
#include <chrono>

int main() {
    auto keys = ApiKeys::load("keys.json");

    KillSwitchGovernor kill;
    PositionTracker tracker;
    BinanceREST rest(keys.api_key, keys.api_secret, keys.dry_run);
    CapitalRouter router(&kill, &rest, &tracker);

    SymbolLane_ANTIPARALYSIS lane;
    FadeETH_WORKING eth;
    CascadeBTC_WORKING btc;
    FundingSniper fund;

    BinanceWSClient ws("wss://stream.binance.com:9443/ws/ethusdt@bookTicker");

    ws.setCallback([&](const std::string& sym, double bid, double ask) {
        lane.onTick(sym, bid, ask);

        if (eth.shouldTrade())
            router.send(sym, "BUY", 0.01, ask, true);

        if (btc.shouldTrade())
            router.send(sym, "SELL", 0.01, bid, true);
    });

    std::cout << "[CHIMERA] MODE B STARTED | "
              << (keys.dry_run ? "DRY" : "LIVE") << std::endl;

    ws.start();
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(10));
}
MAIN

# =====================
# ROOT CMAKE
# =====================
cat > CMakeLists.txt << 'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
  ${ROOT}
  ${ROOT}/core
  ${ROOT}/engines
  ${ROOT}/risk
  ${ROOT}/tier3
  ${ROOT}/metrics
  ${ROOT}/exchange
  ${ROOT}/router
  ${ROOT}/account
)

foreach(f
  ${ROOT}/main.cpp
  ${ROOT}/exchange/BinanceREST.cpp
  ${ROOT}/exchange/BinanceWSClient.cpp
  ${ROOT}/router/CapitalRouter.cpp
  ${ROOT}/account/PositionTracker.cpp
  ${ROOT}/core/SymbolLane_ANTIPARALYSIS.cpp
)
  if(NOT EXISTS ${f})
    message(FATAL_ERROR "Missing required source file: ${f}")
  endif()
endforeach()

add_executable(chimera
  ${ROOT}/main.cpp
  ${ROOT}/exchange/BinanceREST.cpp
  ${ROOT}/exchange/BinanceWSClient.cpp
  ${ROOT}/router/CapitalRouter.cpp
  ${ROOT}/account/PositionTracker.cpp
  ${ROOT}/core/SymbolLane_ANTIPARALYSIS.cpp
)

target_link_libraries(chimera
  OpenSSL::SSL
  OpenSSL::Crypto
  CURL::libcurl
  Threads::Threads
)
CMAKE

echo "[CHIMERA] MODE B FILES INSTALLED"
