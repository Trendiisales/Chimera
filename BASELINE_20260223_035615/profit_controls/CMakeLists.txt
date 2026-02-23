cmake_minimum_required(VERSION 3.16)
project(chimera_profit_controls)

add_library(chimera_profit_controls
    SessionBiasEngine.cpp
    AsymmetricExitEngine.cpp
    LossShutdownEngine.cpp
)

target_include_directories(chimera_profit_controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_profit_controls PROPERTIES CXX_STANDARD 17)
