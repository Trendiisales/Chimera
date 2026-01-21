#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] FORCING CLEAN MODE B LAYER"

mkdir -p exchange router account build

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
    auto res = rest->sendOrder(symbol, side, qty, price, market);

    if (res == "DRY_RUN_OK") {
        tracker->onFill(symbol,
                          side == "BUY" ? qty : -qty,
                          price);
    }
}
CRC

# =====================
# exchange/BinanceWSClient.hpp
# =====================
cat > exchange/BinanceWSClient.hpp << 'BWH'
#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

class BinanceWSClient {
public:
    using TickCB = std::function<void(const std::string&, double, double)>;

private:
    TickCB cb;
    std::thread worker;
    std::atomic<bool> running{false};

public:
    BinanceWSClient(const std::string&) {}

    void setCallback(TickCB f) { cb = f; }

    void start() {
        running = true;
        worker = std::thread([&]() {
            while (running) {
                if (cb) {
                    cb("ETHUSDT", 0.0, 0.0);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

    void stop() {
        running = false;
        if (worker.joinable()) worker.join();
    }
};
BWH

# =====================
# exchange/BinanceWSClient.cpp
# =====================
cat > exchange/BinanceWSClient.cpp << 'BWC'
#include "BinanceWSClient.hpp"
BWC

# =====================
# main.cpp
# =====================
cat > main.cpp << 'MAIN'
#include "account/ApiKeys.hpp"
#include "account/PositionTracker.hpp"
#include "exchange/BinanceWSClient.hpp"
#include "exchange/BinanceREST.hpp"
#include "router/CapitalRouter.hpp"
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

    BinanceWSClient ws("STUB");

    ws.setCallback([&](const std::string& sym, double bid, double ask) {
        std::cout << "[TICK] " << sym
                  << " bid=" << bid
                  << " ask=" << ask << std::endl;

        router.send(sym, "BUY", 0.001, ask, true);
    });

    std::cout << "[CHIMERA] MODE B CORE RUNNING | "
              << (keys.dry_run ? "DRY" : "LIVE")
              << std::endl;

    ws.start();
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(10));
}
MAIN

# =====================
# CMakeLists.txt
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
)

target_link_libraries(chimera
  OpenSSL::SSL
  OpenSSL::Crypto
  CURL::libcurl
  Threads::Threads
)
CMAKE

echo "[CHIMERA] MODE B CLEAN LAYER WRITTEN"
