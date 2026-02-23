cmake_minimum_required(VERSION 3.16)
project(chimera_exec_escalation)

add_library(chimera_exec_escalation
    TakerEscalationEngine.cpp
    EscalationSinkStdout.cpp
)

target_include_directories(chimera_exec_escalation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_exec_escalation PROPERTIES CXX_STANDARD 17)
