cmake_minimum_required(VERSION 3.16)
project(chimera_exec_policy)

add_library(chimera_exec_policy
    ExecPolicyGovernor.cpp
    ExecPolicySinkStdout.cpp
)

target_include_directories(chimera_exec_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_exec_policy PROPERTIES CXX_STANDARD 17)
