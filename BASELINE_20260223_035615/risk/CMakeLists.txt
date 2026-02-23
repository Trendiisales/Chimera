cmake_minimum_required(VERSION 3.16)
project(chimera_risk)

add_library(chimera_risk
    CapitalAllocator.cpp
    CapitalSinkStdout.cpp
    LossPatternDetector.cpp
)

target_include_directories(chimera_risk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_risk PROPERTIES CXX_STANDARD 17)
