cmake_minimum_required(VERSION 3.16)
project(chimera_hardening)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hardening
    src/TradeAuthority.cpp
    src/ReplayModeControl.cpp
    src/MarketStateLogger.cpp
    src/ShapleyEngine.cpp
    src/Secrets.cpp
    src/LineageTracker.cpp
)

target_include_directories(hardening PUBLIC include)
