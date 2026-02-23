cmake_minimum_required(VERSION 3.15)

project(ChimeraMetals VERSION 1.0.0 LANGUAGES CXX)

# ==================== COMPILER SETTINGS ====================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# Optimization flags
if(MSVC)
    add_compile_options(/W4 /O2 /GL /arch:AVX2)
else()
    add_compile_options(-Wall -Wextra -O3 -march=native)
endif()

# ==================== DEPENDENCIES ====================

# OpenSSL
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# ==================== INCLUDE DIRECTORIES ====================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/engines
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/allocation
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/spine
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/infra
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/core
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/integration
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/sizing
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/latency
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/replay
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/profit_controls
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/exec_policy
    ${CMAKE_CURRENT_SOURCE_DIR}/BASELINE_20260223_035615/exec_escalation
    ${OPENSSL_INCLUDE_DIR}
)

# ==================== BASELINE LIBRARIES ====================

# Risk Module
add_library(chimera_baseline_risk STATIC
    BASELINE_20260223_035615/risk/CapitalAllocator.cpp
    BASELINE_20260223_035615/risk/LossPatternDetector.cpp
    BASELINE_20260223_035615/risk/CapitalSinkStdout.cpp
)

# Sizing Module
add_library(chimera_baseline_sizing STATIC
    BASELINE_20260223_035615/sizing/ConfidenceWeightedSizer.cpp
    BASELINE_20260223_035615/sizing/SizingSinkStdout.cpp
)

# Latency Module
add_library(chimera_baseline_latency STATIC
    BASELINE_20260223_035615/latency/LatencyAttributionEngine.cpp
    BASELINE_20260223_035615/latency/TelemetrySinkStdout.cpp
)

# Telemetry Module
add_library(chimera_baseline_telemetry STATIC
    BASELINE_20260223_035615/telemetry/TelemetryBus.cpp
    BASELINE_20260223_035615/telemetry/TelemetryJson.cpp
    BASELINE_20260223_035615/telemetry/TelemetrySinkStdout.cpp
    BASELINE_20260223_035615/telemetry/TelemetryWsServer.cpp
)

# Replay Module
add_library(chimera_baseline_replay STATIC
    BASELINE_20260223_035615/replay/ReplayLog.cpp
    BASELINE_20260223_035615/replay/ReplayRecorder.cpp
    BASELINE_20260223_035615/replay/ReplayEngine.cpp
    BASELINE_20260223_035615/replay/PostTradeAnalyzer.cpp
)

# Profit Controls Module
add_library(chimera_baseline_profit_controls STATIC
    BASELINE_20260223_035615/profit_controls/AsymmetricExitEngine.cpp
    BASELINE_20260223_035615/profit_controls/LossShutdownEngine.cpp
    BASELINE_20260223_035615/profit_controls/SessionBiasEngine.cpp
    BASELINE_20260223_035615/profit_controls/ProfitControlSinkStdout.cpp
)

# Exec Policy Module
add_library(chimera_baseline_exec_policy STATIC
    BASELINE_20260223_035615/exec_policy/ExecPolicyGovernor.cpp
    BASELINE_20260223_035615/exec_policy/ExecPolicySinkStdout.cpp
)

# Exec Escalation Module
add_library(chimera_baseline_exec_escalation STATIC
    BASELINE_20260223_035615/exec_escalation/TakerEscalationEngine.cpp
    BASELINE_20260223_035615/exec_escalation/EscalationSinkStdout.cpp
)

# ==================== CHIMERA EXTENSIONS (Header-Only) ====================

add_library(chimera_extensions INTERFACE)
target_include_directories(chimera_extensions INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/engines
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/allocation
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/spine
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/infra
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/core
    ${CMAKE_CURRENT_SOURCE_DIR}/chimera_extensions/integration
)

# ==================== MAIN EXECUTABLE ====================

add_executable(ChimeraMetals
    src/main_integrated.cpp
)

target_link_libraries(ChimeraMetals
    # Chimera Extensions (header-only)
    chimera_extensions
    
    # Baseline modules
    chimera_baseline_risk
    chimera_baseline_sizing
    chimera_baseline_latency
    chimera_baseline_telemetry
    chimera_baseline_replay
    chimera_baseline_profit_controls
    chimera_baseline_exec_policy
    chimera_baseline_exec_escalation
    
    # System libraries
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(ChimeraMetals
        ws2_32
        wsock32
        crypt32
    )
endif()

# ==================== INSTALLATION ====================

install(TARGETS ChimeraMetals
    RUNTIME DESTINATION bin
)

install(DIRECTORY chimera_extensions/
    DESTINATION include/chimera_extensions
    FILES_MATCHING PATTERN "*.hpp"
)

# ==================== BUILD INFO ====================

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "ChimeraMetals - Complete Integrated Trading System")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  ✓ Metal Structure Engine (XAU/XAG)")
message(STATUS "  ✓ Enhanced Capital Allocator")
message(STATUS "  ✓ Risk Governor")
message(STATUS "  ✓ Telemetry & Attribution")
message(STATUS "  ✓ Execution Spine (Lock-free)")
message(STATUS "  ✓ Unified Coordinator")
message(STATUS "  ✓ FIX Connectivity")
message(STATUS "")
message(STATUS "Baseline Modules:")
message(STATUS "  ✓ Capital Allocator")
message(STATUS "  ✓ Confidence Weighted Sizer")
message(STATUS "  ✓ Latency Attribution")
message(STATUS "  ✓ Telemetry Bus")
message(STATUS "  ✓ Replay Engine")
message(STATUS "  ✓ Profit Controls")
message(STATUS "  ✓ Execution Policy")
message(STATUS "  ✓ Taker Escalation")
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
